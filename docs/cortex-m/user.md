# Cortex-M — user guide

> **Status: DRAFT** · Scope: trusted-firmware · Issue: #161 · Last verified: 2026-10-22
> The Cortex-M boards are emulators: Arm's AN505 (a Cortex-M33) and AN500 (a
> Cortex-M7) in QEMU. No physical Cortex-M board runs this firmware yet.

## What you need

- The Arm toolchain and QEMU, as Debian packages: `gcc-arm-none-eabi`,
  `binutils-arm-none-eabi`, `libnewlib-arm-none-eabi` and `qemu-system-arm`.
  The devcontainer and the CI image have them. Without them a Cortex-M build
  stops and names the missing tool; nothing is skipped.
- A host C compiler and Go, for the signer, the wire checker and the tfwctl
  suite the tests build; `python3` for the wire suite's QMP helpers.

## Build

```
make firmware   BOARD=mps2-an505 CRYPTO=soft
make bootloader BOARD=mps2-an505 BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=0
```

The first builds the application, the second tfw-boot; `BOARD=mps2-an500`
builds the Cortex-M7's. `soft` is the boards' only crypto tier, and every
tfw-boot row verifies a P-256 signature in software. The rows differ in OTA in
boot, the image store and two slots ([architecture.md](architecture.md#what-tfw-boot-verifies));
any other configuration is refused before anything is built.

| Row | Application | tfw-boot |
|---|---|---|
| base | as above | as above |
| store | `OTA_STAGE=1` | `BOOT_STORE=1` |
| two slots | `OTA_STAGE=1 BOOT_AB=1` | `BOOT_STORE=1 BOOT_AB=1` |
| SPI, I2C | `SPI=1` or `I2C=1` | the base row's |

Each row's application builds in a directory of its own, for example
`build/mps2-an505/soft-stage-ab` or `build/mps2-an500/soft-spi`; `make firmware-dir`
prints it.

The images are not usable as built: tfw-boot's key and device secret slots are
empty and the application is unsigned. The tests below provision and sign
them.

## Run the emulation tests

```
make integration-test-cortex-m
make integration-test-cortex-m-wire
```

Neither needs a `BOARD`; each runs mps2-an505 and then mps2-an500, and
`integration-test-cortex-m-<board>` or `integration-test-cortex-m-wire-<board>`
runs one board.

`integration-test-cortex-m` builds the base row, provisions tfw-boot with a
fresh P-256 key and a fresh device secret, signs the application, and boots
three QEMU machines, each with the console on a local socket
(`test/integration/cortex-m/qemu_test.sh`):

| Run | Image | Passes when |
|---|---|---|
| genuine | signed with the provisioned key | it answers `CAPS` with the board's id and tier 1, takes a soft reset, is verified again and answers again |
| tampered | one bit flipped after signing | it stays silent for the whole window (10 s) and QEMU is still running |
| otherkey | signed with a key tfw-boot does not hold | the same |

`integration-test-cortex-m-wire` boots the same signed image with its UART on
a pty and runs the tfwctl wire suite (`make hw-test`) against it, resetting
the guest through QMP where a board's DTR line would
(`test/integration/cortex-m/qemu_wire_test.sh`). It then runs the store, two
slot, SPI and I2C rows. The [README](README.md) lists every row's tests per
board.

What they prove: the core, tfw-boot's verification, OTA through the store and
two slots, and the wire protocol run on two 32-bit Arm cores; a reset goes
back through tfw-boot; and tfw-boot refuses an image that is altered or signed
by the wrong key. What they do not prove: anything about a vendor SoC, real
flash, or a physical board.

Two environment variables change `qemu_test.sh`'s windows: `CM_SILENT`
(default `10s`), how long a refused image must stay silent, and `CM_WAIT`
(default `60s`), how long the genuine image has to answer.

## Troubleshooting

| You see | Cause |
|---|---|
| `arm-none-eabi-gcc is not on PATH` | the toolchain packages are missing |
| `qemu-system-arm is not on PATH` | install `qemu-system-arm` |
| `python3 is not on PATH` | the wire suite reads the pty's path over QMP with Python |
| `... is ARCH avr; run make integration-test-cortex-m` | `cortex-m-qemu-image` was reached with a non-Cortex-M board; call the test target instead |
| `qemu_test: genuine: the signed image did not answer` | tfw-boot refused the genuine image, or the application did not start; the QEMU log's last lines follow the message |
| `qemu_test: tampered: tfw-boot booted an image it must refuse` | a verification failure: treat it as a security defect |
| `QEMU exited: the guest crashed it rather than halting` | the guest faulted QEMU itself; a refusal halts, it never exits |
| `make flash` exits 1 | the board is emulated: QEMU loads the image |

Each run's QEMU log is beside the images, in the board's integration build
directory, as `<run>.qemu.log` (`wire-<run>.qemu.log` for the wire suite).
