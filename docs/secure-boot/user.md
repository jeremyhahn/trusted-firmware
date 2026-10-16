# tfw-boot — usage

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-16
> Conflicts with this document are defects. Fix them here first.

`tfw-boot` is the bootloader that runs before your application on every reset.
This page covers what you do with it as an operator. For every flag see
[cli.md](cli.md); for the wire protocol see [api.md](api.md).

## Prerequisites

- A board with `BOOTRST` programmed and an ISP programmer — `tfw-boot` cannot
  be installed over a serial bootloader, because the boot section is the one
  region a running bootloader cannot write to itself.
- For secure boot rather than measured-only boot, an ATECC608A on the board
  and a device that has been through provisioning
  (see [../device-provisioning/user.md](../device-provisioning/user.md)).

## Build and flash a bootloader

```sh
make bootloader BOARD=atmega328p BOOT_TIER=atecc608a \
     BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=0
make boot-flash BOARD=atmega328p BOOT_TIER=atecc608a \
     BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=0 ISP_PROG=usbasp ISP_PORT=usb
```

`boot-flash` builds what its own variables name, so it takes the same
`BOOT_*` variables as `bootloader`. Without them it would build the defaults,
which fit no board; it refuses them and names the rows that fit. Pick the row
for your board from [cli.md](cli.md)'s table. Every row needs the ATECC608 on
the board. The 328P and the 32U4 have one row, with `BOOT_OTA=0`, so recovery from a bad
image needs a programmer. The `atmega2560` takes `BOOT_OTA=1`
(`BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=1`),
which keeps the device reachable over UART instead. The ATtiny85 has no boot
section and no `tfw-boot`.

Order matters, because `boot-flash` chip-erases the part (see [cli.md](cli.md)):

1. Provision first, through the board's stock bootloader, steps 1 to 6 of
   [../device-provisioning/user.md](../device-provisioning/user.md). Until the
   element holds the image key, `tfw-boot` has nothing to verify an image
   against, the provisioning image included, and stays in recovery.
2. `make boot-flash`. The element keeps its keys, and the application is
   erased. The EEPROM, which holds the platform block, survives the erase only
   with `EESAVE` programmed, so `boot-flash` reads the fuse first and refuses if
   it is not. It prints the `avrdude -U hfuse:w:…:m` that programs `EESAVE` for
   this board; run it and `boot-flash` again. Or pass `BOOT_EEPROM_RESTORE=1`,
   and `boot-flash` saves the EEPROM under `build/`, flashes, writes it back
   and checks it (see [cli.md](cli.md)). Never erase the part another way.
3. Install the signed production image, below.
4. The lock bits, last: they stop ISP writes, and a chip erase clears them.

## Install the application under tfw-boot

The board's stock bootloader is gone once `boot-flash` has run, so `make flash`
with the board's default programmer (`arduino`, `wiring`) has nothing to talk
to. `tfw-boot` speaks no programmer protocol: in recovery a `BOOT_OTA=1` build
answers only `PING`, `RESET` and the `OTA_*` commands ([api.md](api.md)), and
a `BOOT_OTA=0` build answers nothing.

`tfw-boot` boots only an image that carries trailer v1 in the last 128 bytes of
the application region (`TFW_BOOT_TRAILER_ADDR`, `include/tfw/boot.h`), for this
board's id, at or above the floor, whose measurement matches and whose
signature verifies against the image key in the element
(`tfw_boot_evaluate`, `boot/boot_policy.c`). Anything else stays in recovery:
no trailer is `TFW_ERR_UNSUPPORTED`, a wrong measurement or signature
`TFW_ERR_CRYPTO`. `make firmware` writes no trailer, and this tree holds no
signing key and no tool that stamps one: the image key is go-xkms's
([../ota/architecture.md](../ota/architecture.md)). A signed image comes from
there.

There are two routes for a signed image:

- **ISP, after `boot-flash`.** `make flash`'s avrdude line carries `-D`, so it
  writes the application without erasing the boot section. Point it at the ISP:

  ```sh
  make flash BOARD=atmega328p CRYPTO=atecc608a AVRDUDE_PROG=usbasp AVRDUDE_PORT=usb
  ```

  It writes the build's `trusted-firmware.hex`; a signed image is the same
  application with its trailer, written the same way
  (`avrdude -c <isp> -p <part> -D -U flash:w:<signed.hex>:i`). ISP cannot
  program a page that is not erased, so this works only into the blank region
  `boot-flash` leaves. To replace an application over ISP, run `boot-flash`
  again with the same variables and then write the application. Do it before
  the lock bits are set.
- **The OTA session**, on a `BOOT_OTA=1` board (the `atmega2560`), as in
  "Update the application" below. `tfwctl` has no verb for it yet.

Provisioning writes the pinned public-key hash and the initial rollback floor
that `tfw-boot` checks on every boot, and on the element tier places the image
key; a bootloader on a part that was never provisioned has nothing to verify
against and stays in recovery.

## Tell whether a device is running your application or the bootloader

```sh
tfwctl /dev/ttyUSB0 caps
```

A bootloader answers `TFW_ERR_UNKNOWN_CMD` to every application command
including `caps`. `ping` is answered identically by both images, so it
distinguishes "no device" from "device present, in the bootloader."

## Send a device to the bootloader deliberately

```sh
tfwctl /dev/ttyUSB0 reset boot
```

The application resets; with `BOOTRST` programmed, `tfw-boot` always runs
first afterward and decides whether to jump back into the application.

## Update the application (OTA-capable boards only)

There is currently no `tfwctl` verb or Go SDK call for the OTA sequence — see
[cli.md](cli.md)'s "What has no CLI verb." On a board built with
`BOOT_OTA=1`, updating means a host program that frames
`TFW_CMD_OTA_BEGIN`/`OTA_DATA`/`OTA_END` against `sdk/go`'s codec directly (see
[api.md](api.md) for the exact payloads). What to expect once that exists:

To reach the bootloader on a device that is working, send `RESET` with mode 1
(`Device.Reset(ctx, ResetBootloader)`; on the element tier authenticate first,
else `TFW_ERR_AUTH_REQUIRED`). The device restarts into `tfw-boot`, which sends an
`EVENT` with status `TFW_OK` and no payload and answers `PING`. Begin the
sequence within 10 seconds: each accepted `OTA_BEGIN`/`OTA_DATA`/`OTA_END`
restarts that wait, but the whole session is capped at 60 seconds and gets one
attempt -- the first `OTA_END` that does not verify ends it. If a limit runs out
before any page was written the device simply boots the application it already
had; after that the old application is gone, the device lands in recovery, and
you send a good image there. Use near-full 60-byte chunks and answer promptly. A `BOOT_OTA=0` board has no session and just restarts; an
ATtiny85 refuses mode 1 with `TFW_ERR_UNSUPPORTED`.

1. `OTA_BEGIN` declares the whole image length, trailer included.
2. `OTA_DATA` chunks are written as they arrive; nothing is bootable while
   this is in progress.
3. `OTA_END` succeeding means every byte arrived — **not** that the image
   verified. The device measures and checks the signature immediately after
   and reports the result as an unsolicited event.
4. Only a verified image is committed and jumped into. A failed image leaves
   the device in the bootloader, reachable, waiting for another attempt.

There is no rollback to a previous application: the update is written in
place, over the running image, because a 32 KB part has no room for a second
slot. Budget for the device being unreachable-as-an-application between the
first byte written and a successful `OTA_END` — a power loss in that window
leaves the bootloader in charge, not a crash.

## Recover a device that won't leave the bootloader

- **`BOOT_OTA=1` boards** report why over the wire: connect at the board's
  configured baud and read the `EVENT` frame `tfw-boot` sends on entry. The
  status matches [engineering.md](engineering.md)'s error table — most often
  a downgrade (`TFW_ERR_PROTECTED_REGION`), a tampered or unsigned image
  (`TFW_ERR_CRYPTO`), or a device that was never provisioned
  (`TFW_ERR_NOT_PROVISIONED`). Send a new, correctly signed image at or above
  the reported floor.
- **`BOOT_OTA=0` boards** give no signal at all — the device sleeps with
  interrupts off. This is deliberate: the boot section has no room for both a
  framed recovery path and a signature check. Recovery requires an ISP
  programmer and a known-good application image.
- **A device stuck in the bootloader immediately after a fresh flash** almost
  always means provisioning didn't run, or ran against a different key than
  the one this bootloader build was flashed with — re-check the pinned
  public-key hash against what was provisioned.

## What this cannot do

- It cannot downgrade a device below its recorded security version — that is
  the anti-rollback floor working as intended, not a bug to work around.
- It cannot recover a `BOOT_OTA=0` board without physical ISP access.
- It cannot skip more than 16 security versions in one update; an image
  stamped further ahead must be applied through intermediate versions first.
