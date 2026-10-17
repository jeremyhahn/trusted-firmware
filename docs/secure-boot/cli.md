# tfw-boot — CLI

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-16
> Conflicts with this document are defects. Fix them here first.

`tfw-boot` is a bare-metal image with no shell of its own; its CLI is the
`make` targets in `mk/boot.mk` that build, size-check, test and flash it, plus
the one `tfwctl` verb that talks to a bootloader already on a device. See
[api.md](api.md) for the wire commands `tfw-boot` itself answers, and
[../development/building.md](../development/building.md) for the surrounding
build gate.

## `make bootloader` — build tfw-boot and enforce the boot-section budget

```sh
make bootloader BOARD=atmega328p BOOT_TIER=atecc608a \
     BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=0
```

| Flag | Values | Default | Required |
|---|---|---|---|
| `BOARD` | a name under `boards/` (`atmega328p`, `atmega32u4`, `atmega2560`, …) | — | yes |
| `BOOT_TIER` | `soft`, `atecc608a` | `soft` | no |
| `BOOT_MEASURE` | `soft`, `atecc608a` | `soft` | no |
| `BOOT_SIG` | `ed25519-donna`, `atecc608a` | `ed25519-donna` | no |
| `BOOT_OTA` | `0`, `1` | `1` | no |

Produces `build/<board>/boot/<config>/tfw-boot.hex`, where `<config>` encodes
all four variables so two configurations never share objects. Runs
`boot-size-check` as a prerequisite and fails the build — not just the size
report — when the linked image exceeds `BOOT_BUDGET`. A failed link prints the
core/backend breakdown automatically; see `make bootloader-budget` below.

## `make bootloader-check` — build every supported configuration

```sh
make bootloader-check
```

No flags. Iterates `mk/boot.mk`'s `BOOT_SUPPORTED` rows (currently
`atmega328p`, `atmega32u4` and `atmega2560` with `BOOT_TIER=BOOT_MEASURE=
BOOT_SIG=atecc608a`, the 2560 built with OTA, without it, and without it but
with the external store, `BOOT_STORE=1`; and the `atmega2560` soft base row,
`BOOT_TIER=BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=0`) and
fails if any row stops fitting its boot section or its load end
(`__data_load_end`) runs past the part's flash. Part of `make ci`.

## `make bootloader-budget` — the per-component size breakdown

```sh
make bootloader-budget BOARD=atmega328p BOOT_SIG=atecc608a
```

Same flags as `bootloader`. Compiles every core source file and the selected
signature backend individually, without LTO, and prints each one's size,
followed by the linked core size (via `boot-core-size`, with LTO) and the
signature backend's peak stack frame. This is how the numbers in
[architecture.md](architecture.md) and `boot/boot_verify.h` are produced —
reproduced, not asserted.

## `make boot-core-size` — the core alone, without a signature backend

```sh
make boot-core-size BOARD=atmega328p
```

| Flag | Values | Required |
|---|---|---|
| `BOARD` | a board name | yes |

Links the core (measure, OTA/recovery, EEPROM, CDI, boot decision) with
`tfw_boot_verify` and `tfw_boot_pubkey` resolved to address zero by
`--defsym`. **Not a bootloader and not flashable** — it exists only to answer
how much budget a signature backend has to fit in.

## `make bootloader-reproducible` — two builds, byte-identical

```sh
make bootloader-reproducible BOARD=atmega328p BOOT_TIER=atecc608a
```

| Flag | Values | Required |
|---|---|---|
| `BOARD` | a board name | yes |
| `BOOT_TIER` | `soft`, `atecc608a` | no (default `soft`) |

Builds twice in two separate copies of the tree and `cmp`s the resulting
`.hex`. Exits non-zero and prints `tfw-boot NOT REPRODUCIBLE` on any
difference.

## `make boot-flash` — write tfw-boot over ISP

```sh
make boot-flash BOARD=atmega328p BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a \
     BOOT_SIG=atecc608a BOOT_OTA=0 ISP_PROG=usbasp ISP_PORT=usb
```

| Flag | Values | Default | Required |
|---|---|---|---|
| `BOARD` | a board name | — | yes |
| `BOOT_TIER`, `BOOT_MEASURE`, `BOOT_SIG`, `BOOT_OTA` | a supported row for `BOARD`, below | `soft`, `soft`, `ed25519-donna`, `1` | yes: the defaults fit no board |
| `ISP_PROG` | any `avrdude -c` programmer id | `usbasp` | no |
| `ISP_PORT` | any `avrdude -P` port | `usb` | no |
| `BOOT_EEPROM_RESTORE` | `0`, `1` | `0` | no: only when `EESAVE` is not programmed, below |

**Not** the `arduino` bootloader programmer — that one talks *to* a
bootloader, and the boot section is the one region a running bootloader
cannot write to itself. This target requires an ISP programmer.

`bootloader` and `boot-flash` build the configuration their variables name, so
they must name one that fits. Each refuses any other before building anything,
and names the rows that fit `BOARD`. The rows are `BOOT_SUPPORTED` in
`mk/boot.mk`, the set `make bootloader-check` builds. Every element row needs an
ATECC608 on the board's I²C bus: the ATECC608C on the MIKROE-6774 on the bench,
build name `atecc608a`. The soft row needs no element; it verifies the same
P-256 signature in software (`BOOT_SIG=p256-soft`).

Measured with `make bootloader-check` (avr-gcc 14.2.0, tfw-boot built with
`-mcall-prologues`, 2026-10-16); the unsupported row is the `.elf` alone, since
`make bootloader` refuses it:

| Board | Boot section | `BOOT_TIER` `BOOT_MEASURE` `BOOT_SIG` | `BOOT_OTA` | Flash | SRAM | `boot-flash` |
|---|---|---|---|---|---|---|
| `atmega328p` | 4 096 B | `atecc608a` `atecc608a` `atecc608a` | `0` | 3 758 | 351 / 2 048 | yes |
| `atmega32u4` | 4 096 B | `atecc608a` `atecc608a` `atecc608a` | `0` | 3 828 | 351 / 2 560 | yes |
| `atmega2560` | 8 192 B | `atecc608a` `atecc608a` `atecc608a` | `1` | 7 832 | 1 037 / 8 192 | yes: the only row with the serial OTA session (swap-move, trial) |
| `atmega2560` | 8 192 B | `atecc608a` `atecc608a` `atecc608a` | `0` | 3 918 | 351 / 8 192 | yes |
| `atmega2560` | 8 192 B | `atecc608a` `atecc608a` `atecc608a` | `0`, `BOOT_STORE=1` | 6 302 | 869 / 8 192 | yes: the external store's copy, swap-move, trial |
| `atmega2560` | 8 192 B | `atecc608a` `soft` `atecc608a` | `0` | 7 070 | 351 / 8 192 | no: it fits, but it is not a supported row |
| `atmega2560` | 8 192 B | `soft` `soft` `p256-soft` | `0` | 8 190 | — | yes: 2 bytes spare (lane-verify, 935494f) |
| `atmega2560` | 8 192 B | `soft` `soft` `p256-soft` | `0`, `BOOT_STORE=1` | 2 680 over | — | no: not a row; it sheds OTA, never verification |
| `atmega2560` | 8 192 B | `soft` `soft` `p256-soft` | `1` | 3 836 over | — | no: not a row; it sheds OTA, never verification |
| `atmega328p` | 4 096 B | `soft` `soft` `p256-soft` | `0` | 4 030 over | — | no: not a row |
| `atmega32u4` | 4 096 B | `soft` `soft` `p256-soft` | `0` | — | — | no: not a row |
| `attiny85` | none | — | — | — | — | no: the part has no boot section |

Every other combination fails the link on every board, the defaults
(`soft`/`soft`/`ed25519-donna`/`1`) and `BOOT_TIER=atecc608a` alone included.
The soft rows are built at micro-ecc's `UECC_OPT` default, `0` (`mk/boot.mk`). The 2560 soft base
row is the one configuration without the element that fits.

### The chip erase, and the EEPROM

Over ISP a flash page cannot be rewritten without a chip erase, so writing the
boot section erases the part: the application goes, the lock bits are cleared,
and the EEPROM goes too unless `EESAVE` is programmed
([../security/fuses-and-lockbits.md](../security/fuses-and-lockbits.md)). The
EEPROM holds the platform block, so a bare erase loses the device's serial and
with it its identity. `boot-flash` therefore never erases blind. It reads the
fuse that holds `EESAVE` first:

| Board | `EESAVE` | Where it is stated |
|---|---|---|
| `atmega328p`, `atmega32u4`, `atmega2560` | high fuse (`hfuse`) bit 3; programmed when it reads 0 | `BOOT_EESAVE_FUSE`, `BOOT_EESAVE_BIT` in `boards/<board>/profile.mk` |

- **`EESAVE` programmed:** it flashes, and the erase keeps the EEPROM.
- **`EESAVE` not programmed:** it refuses and exits non-zero, printing the two
  ways on:
  - program `EESAVE` first, with the exact command for this board worked out
    from the fuse byte it just read (bit 3 cleared, nothing else changed), e.g.
    for a Mega reading `0xd8`:
    `avrdude -pm2560 -cusbasp -Pusb -U hfuse:w:0xd0:m`;
  - or run it again with `BOOT_EEPROM_RESTORE=1`. It reads the EEPROM to
    `build/eeprom-<board>-<UTC timestamp>.hex`, erases and flashes, writes the
    EEPROM back with `-D`, reads it back and compares. A failure or a mismatch
    exits non-zero and leaves the saved file in place, with the command that
    writes it back. On success the saved copy is kept.

`boot-flash` writes no fuse itself. A board with no `BOOT_EESAVE_*` in its
profile is refused at parse time. How the application then gets onto the board
is in [user.md](user.md).

## `make boot-test` — the host unit suite

```sh
make boot-test
```

No flags. Builds and runs every `test/unit/boot/test_*.c` binary against
`test/mock/boot_hal_mock.c` and reports pass/fail per binary.

## `make integration-test-boot` — the simavr cross-implementation suite

```sh
make integration-test-boot
```

No flags exposed — it recursively invokes `make boot-harness-run` pinned to
`BOARD=atmega2560` with `BOOT_TIER=BOOT_MEASURE=BOOT_SIG=atecc608a
BOOT_OTA=1`, the only row with an OTA recovery path that can report *why* it
refused. Part of `make ci`; requires the `tfwsim`/simavr toolchain the CI image
provides.

## `tfwctl reset boot` — enter the bootloader from a running application

```sh
tfwctl [--baud 9600] [--timeout 10s] /dev/ttyUSB0 reset boot
```

| Flag | Type | Default | Required |
|---|---|---|---|
| `--baud` | int | `serialport.DefaultBaud` | no |
| `--json` | bool | `false` | no |
| `--timeout` | duration | `10s` | no |

Sends `TFW_CMD_RESET` with the bootloader-mode argument; the application
acknowledges, then resets. With `BOOTRST` programmed the next reset always
runs `tfw-boot` first — this verb does not select the bootloader over the
application, it only causes the reset that `tfw-boot` then evaluates.

## What has no CLI verb

**No verb sends `TFW_CMD_OTA_BEGIN`/`OTA_DATA`/`OTA_END`/`OTA_STATUS`.**
`sdk/go/commands.go` and `sdk/go/cmd/tfwctl` implement the application's
command set only; the OTA session `tfw-boot` answers
(see [api.md](api.md)) has no Go SDK binding and no `tfwctl` subcommand today.
Driving an update currently means writing a host program directly against
`sdk/go`'s frame codec (`sdk/go/codec.go`) — a real gap, not an omission from
this page.
