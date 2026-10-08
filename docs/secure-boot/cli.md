# tfw-boot — CLI

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
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
BOOT_SIG=atecc608a`, the 2560 built both with and without OTA) and fails if
any row stops fitting its boot section. Part of `make ci`.

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
make boot-flash BOARD=atmega328p ISP_PROG=usbasp ISP_PORT=usb
```

| Flag | Values | Default | Required |
|---|---|---|---|
| `BOARD` | a board name | — | yes |
| `ISP_PROG` | any `avrdude -c` programmer id | `usbasp` | no |
| `ISP_PORT` | any `avrdude -P` port | `usb` | no |

**Not** the `arduino` bootloader programmer — that one talks *to* a
bootloader, and the boot section is the one region a running bootloader
cannot write to itself. This target requires an ISP programmer.

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
