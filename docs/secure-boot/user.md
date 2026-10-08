# tfw-boot — usage

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
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
make boot-flash BOARD=atmega328p ISP_PROG=usbasp ISP_PORT=usb
```

Pick the combination for your board from [cli.md](cli.md)'s supported set —
not every combination fits every part's boot section, and the build fails
with the size overage rather than producing an image that doesn't work.
`BOOT_OTA=0` on a 4 KB part (the 328P, the 32U4) means recovery from a bad
image needs a programmer; `BOOT_OTA=1` on the 8 KB `atmega2560` keeps the
device reachable over UART instead.

Then flash and provision the application the normal way — see
[../development/flashing.md](../development/flashing.md) and
[../device-provisioning/user.md](../device-provisioning/user.md). Provisioning
writes the pinned public-key hash and the initial rollback floor that
`tfw-boot` checks on every boot; a bootloader flashed before provisioning has
nothing to verify against and stays in recovery.

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
