# Cortex-M — architecture

> **Status: DRAFT** · Scope: trusted-firmware · Issue: #161 · Last verified: 2026-10-22
> The decisions and their reasons are in the two ADRs,
> [adr-reference-part-and-emulation.md](adr-reference-part-and-emulation.md) and
> [adr-trust-model-vs-tf-m.md](adr-trust-model-vs-tf-m.md), both PROPOSED. This
> page describes what the row has built from them; what each slice added, and
> the QEMU rows that test it, are in the [README](README.md).

## The problem

The core, the wire protocol and tfw-boot were written for an 8-bit AVR. A
32-bit core must run the same code with no `#if` in it, and a board must be
data, not a fork. The row proves this on two cores as QEMU models them, with
no vendor SoC: the Cortex-M33 in Arm's AN505 (`qemu-system-arm -M mps2-an505`)
and the Cortex-M7 in Arm's AN500 (`-M mps2-an500`).

## Ownership boundary

This repo owns the port: the architecture's make file, the core HAL, the SoC
files, tfw-boot's hardware layer and the boards. It does not own TF-M, any
vendor SDK or CMSIS. None of them is linked, and `lint-arch-leak` keeps
Cortex-M names out of the tree outside the port's own directories (see
[engineering.md](engineering.md)). DICE profiles and evidence verification stay
with go-xkms, as for every other architecture.

## Three layers: architecture, SoC, board

```
 mk/arch/cortex-m.mk            the architecture: toolchain, flags, placement
        |
 src/hal/cortex-m/              the core: reset, vectors, SysTick, SCB, faults,
 boot/hal/cortex-m/             the handoff, the platform block, the jump, the store
        |
 src/hal/cortex-m/<SOC>/        the SoC's peripherals: UART, GPIO, ADC, watchdog,
                                the SBCon (I2C) and the PL022 (SPI)
 boot/hal/cortex-m/<SOC>/       tfw-boot's polled UART
 mk/arch/cortex-m/<SOC>.ld      one linker script per SoC, for both images
        |
 boards/<board>/                data: profile.mk, board_profile.h, board.c
```

- **The architecture.** A board's `profile.mk` says `ARCH := cortex-m`, the
  core in `CPU` (`cortex-m33` or `cortex-m7`) and the SoC directory in `SOC`
  (`mps2` for both boards). `mk/firmware.mk` then includes
  `mk/arch/cortex-m.mk`, which fills the same contract `mk/arch/avr.mk` does:
  `ARCH_CC`, `ARCH_LDFLAGS`, the RAM count, the placement flags, the flash
  commands.
- **The SoC.** `mk/firmware.mk` sets `HAL_SOC_DIR` to `src/hal/cortex-m/$(SOC)`;
  `hal_src` prefers a SoC copy of a module over the core's. `mk/boot.mk`'s
  `BOOT_SOC_SRCS` adds `boot/hal/cortex-m/$(SOC)/*.c` and the SoC's
  `hal_soc.c`, so the watchdog code exists once and both images link it. The
  two MPS2 boards share the SoC directory because their peripherals are the
  same CMSDK blocks at different addresses, which each `board_profile.h` names.
- **The board.** `boards/mps2-an505/` and `boards/mps2-an500/` hold the memory
  map, the budgets, the pin table and the feature switches. Board ids `0x40`
  and `0x42` are in [`../boards/board-id-registry.md`](../boards/board-id-registry.md).

## The HAL is substituted at link time

As on AVR, there is no vtable. `include/tfw/hal.h` declares the functions; the
build links exactly one definition of each from the architecture's directory.
A capability the board turns off in `board_profile.h` (`TFW_FEATURE_EEPROM 0`,
`TFW_FEATURE_PWM 0`, …) is dropped by file selection in `mk/firmware.mk`, so
its HAL entry points are neither defined nor referenced. A feature a board turns
on only for a named row (`I2C=1`, `SPI=1`) is listed in `BOARD_FEATURES_ON` in
`profile.mk`, and the row builds in a directory of its own (`-i2c`, `-spi`).
Which functions this port defines and which it leaves out is in [api.md](api.md).

## Memory map

The AN505's addresses are the Secure alias: the SAU is off, so the core runs
Secure and sees the Secure map (`boards/mps2-an505/profile.mk`). The AN500's
Cortex-M7 has no TrustZone.

| Region | mps2-an505 | mps2-an500 | Holds |
|---|---|---|---|
| tfw-boot | `0x10000000` (SSRAM-0) | `0x00000000` (ZBT SSRAM1) | tfw-boot's 32 KiB region, where the reset vector points |
| application | `0x38000000` (SSRAM-1/2) | `0x00008000` | the 128 KiB slot, its trailer at the slot's end |
| image store | `0x38200000` | `0x00200000` | 264 KiB, the stager's store ([README](README.md#the-image-store)) |
| platform block | `0x383FF000` | `0x003FF000` | 4 KiB (`TFW_CM_PLATFORM_BASE`) |
| SRAM | `0x30000000`, 32 KiB | `0x20000000`, 4 MiB | `.data`, `.bss`, the stack, the fault record, the reset marker, the handoff block |

The two-slot build (`BOOT_AB=1`) moves the application to `0x38040000` and
`0x00040000` and loads factory copies of the application and the platform
block, which tfw-boot copies home ([README](README.md#two-slots)).

The code regions are RAM that QEMU loads. QEMU copies the images it loaded in
again on every system reset, so the store and the platform block, which it
does not load, survive a reset; nothing survives a new QEMU.

## Placement: one linker script, two images

`mk/arch/cortex-m/mps2.ld` has no `MEMORY` block and no address.
`mk/arch/cortex-m.mk` passes each as a symbol from the profile
(`__tfw_text_base`, `__tfw_text_size`, `__tfw_ram_base`, `__tfw_handoff`,
`__tfw_resetmark`, `__tfw_faultframe`, `__stack`), so the script cannot
disagree with the board.

- The vector table (`.vectors`) is first in the code region.
- `.data`'s initial values follow the code and the reset handler copies them,
  so a board whose code region is flash takes the same script.
- From the one formula in `mk/arch/cortex-m.mk`, in both images: the handoff
  block at `RAM_END - 111` (112 bytes), the reset marker at `RAM_END - 115`
  and the 12-byte fault record at `RAM_END - 127`. The stack top is below
  them, 8-aligned (AAPCS).
- The link's code length is the budget, and the script asserts that `.data`'s
  load image ends inside it, so an image past it is `ld`'s error.

## Boot flow

1. The core loads the stack pointer and the reset handler from tfw-boot's
   table. The reset handler, `tfw_cm_reset`, is C, in
   `src/hal/cortex-m/hal_early.c`, which both images link (`-nostartfiles`,
   no crt0). It reads and clears the reset marker, disarms the watchdog
   (`tfw_cm_soc_early`), points VTOR at its own table, copies `.data`, zeroes
   `.bss`, decodes the marker and calls `main`.
2. tfw-boot reads the platform block, measures the application with SHA-256
   in software, and verifies the trailer's ECDSA P-256 signature in software
   (micro-ecc, `BOOT_SIG=p256-soft`) against the key in its provisioned slot,
   whose hash the platform block pins.
3. On success it writes the CDI handoff block and jumps:
   `tfw_boot_jump_application` (`boot/hal/cortex-m/boot_hal_cortex-m.c`) turns
   its UART off, loads MSP from the application's word 0 and branches to word
   1. The application's own reset handler sets VTOR.
4. On a refusal, a row without OTA in boot links
   `boot/hal/cortex-m/recovery_halt.c`, which masks interrupts and sleeps in
   WFI forever. The an505's `…:1:0` row links the recovery session instead
   (`BOOT_OTA_SRCS` in `mk/boot.mk`).

The application reads the reset cause from the handoff block first, and falls
back to its own latch (`src/hal/cortex-m/hal_sys.c`). A Cortex-M defines no
reset-cause register, and QEMU's MPS2 machines model none, so the cause is a
marker word in SRAM that whoever causes a reset writes first
(`src/hal/cortex-m/hal_early.h`), decoded by
`src/hal/cortex-m/hal_reset_decode.h`: `SOFTWARE` for a commanded reset,
`PANIC` for a fault, `WATCHDOG` for the watchdog's bite.

## What tfw-boot verifies

The boards' tfw-boot rows (`mk/boot.mk`, `BOOT_SUPPORTED`):

```
mps2-an505:soft:soft:p256-soft:0:0
mps2-an505:soft:soft:p256-soft:1:0
mps2-an505:soft:soft:p256-soft:0:1
mps2-an505:soft:soft:p256-soft:0:1:ab
mps2-an500:soft:soft:p256-soft:0:0
mps2-an500:soft:soft:p256-soft:0:1
mps2-an500:soft:soft:p256-soft:0:1:ab
```

Every row is the soft tier and soft measurement with ECDSA P-256 verified in
software; they differ in OTA in boot, the store and two slots. Every tfw-boot
row verifies a signature, the soft one included; there is no `BOOT_SIG=none`
row. The boards' only crypto tier is `soft` (`BOARD_TIERS := soft`): the UDS is
in tfw-boot's soft slot (`boot/tier/cdi_soft.c`), and no secure element is on
either board.

## Trust boundaries

- The root of trust is tfw-boot's code region and its two soft slots, the
  P-256 public key and the UDS. They are written into the image after the link
  (`test/integration/cortex-m/cm_image.c` does it for the test).
- The port programs no MPU and no SAU. Everything runs Secure and privileged,
  and nothing in hardware stops the application from reading tfw-boot's
  region. That is the open MPU question in [engineering.md](engineering.md),
  and the isolation design is
  [adr-trust-model-vs-tf-m.md](adr-trust-model-vs-tf-m.md)'s.
- `tfw_boot_flash_write_page` refuses any page outside the application region.
  tfw-boot's region is writable RAM on these boards, so that range check is
  the only thing that protects it.

## What the row does not do yet

- No vendor SoC and no i.MX 95. QEMU has no i.MX 95.
- No TF-M, no Non-secure world, no MPU.
- No secure element on QEMU: mps2-an505's `atecc608a:I2C=1` row is built and
  size-checked, never booted.
- No PWM, tone, pulse, EEPROM or 1-Wire.
- No `make flash`. The boards are emulated, and `make flash` says so and
  exits 1 (`ARCH_CM_NOFLASH`).

## Rejected alternatives

| Alternative | Why not |
|---|---|
| A HAL vtable selected at run time | Costs RAM, and every entry stays a live reference, so `--gc-sections` cannot drop unused peripherals (`CLAUDE.md`) |
| CMSIS headers and a vendor startup file | A second copy of the vector table and the reset path; the port's own `cm_regs.h` names the registers it uses, and `lint-arch-leak` keeps them in the port |
| An assembly crt0 | The reset handler in C touches no global until it has made them, and one file serves both images |
| A linker script per board | The addresses are the profile's; a script that held them would be a second place to change them |
| Hard-float ABI | No image uses floating point, and the FPU's lazy-stacking state would be one more thing tfw-boot hands across (`-mfloat-abi=soft` in `mk/arch/cortex-m.mk`) |
