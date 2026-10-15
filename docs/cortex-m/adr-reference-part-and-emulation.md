# ADR: the Cortex-M reference part and its CI emulation target

> **Status: PROPOSED (2026-10-15)** · Issue: #14 · SPEC rows: `cortex-m` (future),
> `imx95`, `stm32h5` · Scope: trusted-firmware only · Last verified: 2026-10-15
>
> Documentation only. No code, build file, board id or SPEC row changes with this
> ADR. Every number marked *confirm* comes from a document the lane could not
> read (see "Sources"), and is a starting point for the owner, not a fact.

## The question

The SPEC row `cortex-m` says the Cortex-M ports are generic architecture ports,
Cortex-M7 and Cortex-M33 first, each a HAL and a boot-HAL port, with boards as
data. The owner ruled at 19:48Z (DS-1704) that the i.MX 95's
Cortex-M7 is a board. Until the hardware arrives, the Cortex-M targets run in
QEMU.

This ADR decides four things:

1. which part the first port is written and checked against;
2. which QEMU machine stands in for it in `make ci`, and what that machine can
   and cannot prove;
3. how the port maps onto the seams that already exist;
4. the toolchain, the budgets and the boot chain.

The trust model, and how it relates to TF-M, is #15 and is not decided here.

## Context

- **The HAL is a link-time contract.** `include/tfw/hal.h` is implemented once
  per architecture under `src/hal/<arch>/`, and `boot/boot_hal.h` under
  `boot/hal/<arch>/`. The mock implements both. Nothing is a vtable, and a
  feature is in or out by whole `.c` files in the object list.
- **Adding an architecture is three things.**
  - `mk/arch/<arch>.mk` sets `ARCH_CC`, `ARCH_OBJCOPY`, `ARCH_SIZE`,
    `ARCH_LIBC`, `ARCH_MACH_CFLAGS`, `ARCH_OPT_CFLAGS`, `ARCH_LDFLAGS`,
    `ARCH_FW_PLACEMENT_LDFLAGS`, `ARCH_BOOT_PLACEMENT_LDFLAGS`, `ARCH_FLASH`
    and `ARCH_BOOT_FLASH`.
  - A `src/hal/<arch>/` directory.
  - A `boot/hal/<arch>/` directory.

  A board's `profile.mk` names its `ARCH` (see `mk/arch/avr.mk`).
- **`#if` is confined.** It is allowed only in `include/tfw/config.h`,
  `boards/*/board_profile.h`, `src/core/commands.def`, `src/hal/*/` and
  `boot/hal/*/`, plus `include/tfw/progmem.h`.
  - The lint gate's exemption already matches nested directories such as
    `src/hal/cortex-m/<soc>/`.
- **Two placements are fixed by contract.**
  - The CDI handoff block is 80 bytes at `RAM_END - 79`, and both images place
    it with the same linker formula.
  - The platform block is read through `tfw_hal_nvm_read`. Only tfw-boot writes
    it.
- **A board with no tfw-boot has a precedent.** The ATtiny85's `profile.mk`
  defines no `BOOT_SECTION` and no `RAM_END`, so it has neither a bootloader nor
  a handoff.
- **Board ids are one byte.** The Cortex-M range is `0x40`–`0x7F`, and no id in
  it is allocated yet. Ids are allocated in the registry and in
  `sdk/go/boardid.go` in the same change. They never renumber.
- **The integration tier runs the real image.** It runs under simavr
  (`mk/integration.mk`), and every harness has a hard cycle bound.
  `qemu-system-avr` was rejected for AVR because it has no peripherals. That
  reason does not carry over to Arm: QEMU models the MPS2 peripherals well.
- **CI is one Debian image.** The image is `debian:trixie-slim`. Its packages
  come only from `docker/packages.txt`, and the image records the toolchain
  versions in `/etc/ci-toolchain.txt`. A Debian-only toolchain is a deliberate
  shorter trust chain.

## Constraints

- **No skip guards.** A Cortex-M test that cannot find its toolchain or its
  emulator fails and says what to install.
- **No `#if` outside the four places.** A per-CPU or per-SoC difference is a
  make conditional in `mk/arch/cortex-m.mk`, a whole file under
  `src/hal/cortex-m/<soc>/`, or a macro in `board_profile.h`.
- **No private keys in this repository's images or SDK.** On the i.MX 95, keys
  live in the EdgeLock secure enclave (ELE), which mirrors the AVR element tier.
- **Ownership on the i.MX 95.** trusted-imx95 owns the platform image, the
  AHAB boot container and the System Manager (SM) configuration. This repository
  owns only the M7 firmware and its board profile (SPEC row `imx95`).
- **The M7 must not depend on Linux.** SPEC row `hsm-self-defence` requires
  that a compromised A55 cannot stop the M7 enforcing. The M7 is therefore
  started by the ROM and the SM, never by Linux `remoteproc`.

## Decisions

### 1. The reference part: the i.MX 95 Cortex-M7

The first Cortex-M port is written against the **Cortex-M7 of the NXP i.MX 95**
on the FRDM-IMX95 board (SPEC row `imx95`), as the owner ruled. The second
target is a Cortex-M33 with TrustZone-M: the STM32H5 (SPEC row `stm32h5`).

| | i.MX 95 Cortex-M7 (reference) | STM32H5 Cortex-M33 (second) |
|---|---|---|
| Architecture | Armv7E-M, no Security Extension | Armv8-M Mainline with the Security Extension (TrustZone-M) |
| Where the code runs | TCM, loaded from the AHAB container; the core has no on-chip flash | on-chip flash, executed in place |
| Root of trust | ROM + ELE: AHAB verifies the container against the fused SRK hash | ST's immutable root of trust; Secure Manager on the -DK, our own secure world on the NUCLEO |
| Where keys live | ELE, outside the core | Secure Manager or our secure world (#15) |
| Isolation from other masters | SoC: TRDC configured by the SM; a separate core from the A55 | TrustZone-M (SAU/IDAU), plus MPCBB/TZSC configured by the secure world |
| tfw-boot | none in the first port; L0 is NXP's ROM, ELE and SM | plausible: on-chip flash, our own L0 |
| What we write that is not the HAL | nothing below the firmware | a secure-world image, or an integration with ST's (#15) |

Why the M7 goes first:

- **The ruling says so**, and the M7 is the core that `hsm-self-defence` needs.
- **The trust model is closest to what works today.** Keys stay in a separate
  element (ELE, as the ATECC608A today), and the firmware only asks for
  operations. The Cortex-M port does not have to wait for the TrustZone and TF-M
  decision in #15.
- **The core HAL carries over.** SysTick, NVIC, SCB, the MPU and the vector
  table are architecturally the same in Armv7-M and in the Non-secure state of
  Armv8-M Mainline. The `src/hal/cortex-m/` core is shared, and only the
  per-CPU flags and the SoC files differ.

What it costs:

- **The M7 port does not exercise tfw-boot.** The first tfw-boot on Cortex-M
  is the QEMU board (decision 5), then the STM32H5.
- **The port depends on trusted-imx95.** That repository must assign the M7 a
  logical machine in the SM configuration, and place the M7 image in the boot
  container. Until it does, the port is QEMU-only. This is a dependency to
  report, not work to do here.
- **No QEMU model exists.** QEMU has no i.MX 95, and no model of the ELE, the
  SM or the TRDC. CI therefore proves the core and the protocol, never the SoC
  (decision 3).

### 2. One architecture, `cortex-m`; CPUs and SoCs are data

```
mk/arch/cortex-m.mk          ARCH := cortex-m; picks -mcpu/-mfpu by $(CPU)
src/hal/cortex-m/            the core: shared by every Cortex-M part
src/hal/cortex-m/mps2/       CMSDK UART, FPGAIO, PL022, SBCon (QEMU board)
src/hal/cortex-m/imx95/      LPUART, RGPIO, LPI2C, LPSPI, TPM, SCMI-over-MU client
boot/hal/cortex-m/           the boot HAL core
boot/hal/cortex-m/mps2/      pseudo-flash and NVM on MPS2 RAM (decision 5)
boards/mps2-an500/           profile.mk: ARCH := cortex-m, CPU := cortex-m7, SOC := mps2
boards/frdm-imx95-m7/        profile.mk: ARCH := cortex-m, CPU := cortex-m7, SOC := imx95
```

- `profile.mk` gains `CPU` and `SOC` beside `ARCH`.
- `mk/arch/cortex-m.mk` adds `src/hal/cortex-m/$(SOC)/*.c` to the HAL object
  list. Selection is by make, and the C has no `#if`.
- A Cortex-M33 board reuses `src/hal/cortex-m/` unchanged. It adds its SoC
  directory and sets `CPU := cortex-m33`.
- The secure world, if we write one, is #15's design, not this layout's.

### 3. The CI emulation target: QEMU `mps2-an500`

The Cortex-M7 CI target is **QEMU `mps2-an500`**: the Arm MPS2 board with the
AN500 FPGA image, a Cortex-M7. It is the only Cortex-M7 machine in QEMU 10.0.

The facts below are read from `hw/arm/mps2.c` at QEMU v10.0.0, which is the
version Debian trixie ships.

**Memory.** Everything is RAM. The board has no flash controller.

| Region | Address | Size |
|---|---|---|
| ZBT SSRAM1 | `0x00000000` | 4 MiB, mirrored at `0x00400000` |
| SSRAM2/3 | `0x20000000` | 4 MiB, mirrored at `0x20400000` |
| PSRAM | `0x60000000` | 16 MiB |

**Clocks.** SYSCLK is 25 MHz, and the SysTick reference clock is 1 MHz.

**Interrupts.** The NVIC has 32 external IRQs.

**Peripherals.**

| Peripheral | Address | Interrupt | Notes |
|---|---|---|---|
| CMSDK APB UART ×5 | `0x40004000`, `0x40005000`, `0x40006000`, `0x40007000`, `0x40009000` | UART0: RX on IRQ 0, TX on IRQ 1 | UART0 is the first `-serial` |
| CMSDK APB timers ×2 | `0x40000000`, `0x40001000` | IRQs 8 and 9 | |
| CMSDK dual timer | `0x40002000` | IRQ 10 | |
| CMSDK watchdog | `0x40008000` | NMI | |
| FPGAIO (LEDs, switches) | `0x40028000` | | |
| PL022 SPI ×5 | `0x40020000`, `0x40021000`, `0x40025000`, `0x40026000`, `0x40027000` | | |
| SBCon bit-banged I2C ×4 | `0x40022000`, `0x40023000`, `0x40029000`, `0x4002a000` | | |
| LAN9118 Ethernet | | | |

The CMSDK GPIO block at `0x40010000`–`0x40013000` is an *unimplemented device*.
It reads as zero and ignores writes.

**What it proves:**

- **Core bring-up:**
  - the vector table and `VTOR`;
  - the reset handler and the stack;
  - `.data` copy and `.bss` zeroing.
- **Exceptions:**
  - NVIC priorities and enables;
  - the HardFault path;
  - the watchdog NMI.
- **The tick and timing:** `tfw_hal_millis` from SysTick, and `tfw_hal_delay_us`
  by polling SysTick, both in virtual time under `-icount`.
- **The wire protocol end to end on the real image, over CMSDK UART0:**
  - framing and resync;
  - PING, CAPS and SYS_INFO;
  - the CRC-error and unknown-command paths;
  - the scheduler.
- **Reset:** reset via `SCB->AIRCR.SYSRESETREQ`, and the reset-cause path
  through the HAL.
- **The MPU (PMSAv7)**, if the port uses one (see open question 7).
- **Crypto and boot logic:**
  - the `none` and `soft` crypto tiers;
  - tfw-boot's verify, measure and handoff logic, on the pseudo-flash of
    decision 5;
  - the handoff block's placement.
- **Buses:**
  - SPI through the PL022;
  - I2C through the SBCon bit-bang, where `tfw_hal_delay_us` is the protocol.

**What it cannot prove**, and which tier covers it:

| Not modelled | Covered by |
|---|---|
| GPIO (the block is unimplemented), ADC, PWM, tone, pulse capture, one-wire | the bench, on hardware |
| EEPROM; any flash controller, erase or wear | the bench; the boot-HAL unit tests on the mock |
| The i.MX 95 itself: ELE, AHAB, SM, SCMI, MU, TRDC, LPUART, LPI2C, LPSPI, TPM | the bench on the FRDM-IMX95, and trusted-imx95's own tests |
| Caches, TCM timing, bus contention, cycle-accurate timing | the bench; there is no Cortex-M bench tier until hardware |
| Real interrupt latency under load | the bench |

The FPGAIO LEDs and switches give the board a few real pins, so the GPIO wire
path has something to write to. The SoC capability files for ADC, PWM, tone,
pulse, one-wire and EEPROM are left out of the object list for `mps2-an500`, so
those capabilities report `TFW_ERR_UNSUPPORTED`. They do not pretend.

**How it runs in `make ci` without a skip guard:**

- **Packages.** `docker/packages.txt` gains `gcc-arm-none-eabi`,
  `binutils-arm-none-eabi`, `libnewlib-arm-none-eabi` and `qemu-system-arm`.
  Their versions are recorded in `/etc/ci-toolchain.txt` the way avr-gcc's are.
- **The target.** A new `integration-test-cortex-m` target lives in its own
  `mk/integration-cortex-m.mk`, and `ci:` lists it after
  `integration-test-boot`.
  - It builds `BOARD=mps2-an500`.
  - It runs a harness in `test/integration/qemu/`.
  - The harness speaks the same frames and makes the same assertions as the
    simavr harness, through the same `src/proto/crc16.c`.
- **The QEMU command.**
  `qemu-system-arm -M mps2-an500 -nographic -icount shift=0,sleep=off -kernel <elf>`.
  UART0 goes to a Unix-socket chardev that the harness owns.
  - `-icount` makes virtual time a function of instructions retired, so a run
    is deterministic.
  - The harness enforces a hard bound on wall-clock time and on virtual time.
    Exceeding either is a failure, as the cycle bound is for simavr.
- **No skip.** If `qemu-system-arm` or `arm-none-eabi-gcc` is not on `PATH`,
  the target stops with an error that names the Debian package. It never prints
  a skip.
- **Sweeps and checks.** `board-sweep`, `firmware-matrix` and
  `reproducible-check` pick up the board like any other, because it is a
  directory under `boards/`.

**The M33 later.** The Cortex-M33 emulation target is **QEMU `mps2-an505`**: a
single Cortex-M33 with the Security Extension, an IDAU, and the MPC and PPC
security controllers. `mps2-an521` is the dual-core variant. Both are what TF-M
and Zephyr test against. The M33 target lands with the STM32H5 port or with
#15, not with this one.

### 4. The HAL, function by function

"Core" means `src/hal/cortex-m/`. The two right-hand columns are the SoC files.

| `hal.h` function | Where | `mps2-an500` (QEMU) | `frdm-imx95-m7` |
|---|---|---|---|
| `tfw_hal_init` | core + SoC | SysTick; UART0; FPGAIO | SysTick; LPUART; clocks and pins requested from the SM over SCMI |
| `tfw_hal_reset_cause` | SoC | a `.noinit` marker set before `SYSRESETREQ`; otherwise `POWER_ON` | the SM's reset reason for the M7 logical machine, over SCMI |
| `tfw_hal_reset` | core | `AIRCR.SYSRESETREQ` | an SCMI logical-machine reset request; what it resets is SM policy |
| `tfw_hal_irq_enable` | core | `cpsie i` | `cpsie i` |
| `tfw_hal_millis` | core | SysTick at 1 kHz | SysTick at 1 kHz |
| `tfw_hal_delay_us` | core | busy-poll of SysTick `VAL` (no DWT: QEMU does not model `CYCCNT`) | the same |
| `tfw_hal_uart_init`, `_rx`, `_tx_try`, `_tx_idle` | SoC | CMSDK UART0, interrupt-driven RX | the LPUART the SM assigns to the M7 (open question 3) |
| `tfw_hal_gpio_mode`, `_write`, `_read` | SoC | FPGAIO LEDs (out) and switches (in) | RGPIO; pin mux from the SM |
| `tfw_hal_adc_init`, `_set_ref`, `_read` | SoC | not linked | SAR ADC, if the SM assigns it |
| `tfw_hal_pwm_write`, `_stop` | SoC | not linked | TPM |
| `tfw_hal_tone_start`, `_stop` | SoC | not linked | TPM |
| `tfw_hal_pulse_in`, `tfw_hal_pulse_capture` | SoC | not linked | TPM input capture |
| `tfw_hal_eeprom_read`, `_write` | — | not linked; `TFW_EEPROM_SIZE 0` | not linked; `TFW_EEPROM_SIZE 0` |
| `tfw_hal_nvm_read` | SoC | the platform block in PSRAM (decision 5) | open question 4 |
| `tfw_hal_handoff_block` | core | the linker-placed `.handoff` at `RAM_END - 79` | none: no tfw-boot, so no handoff (as the ATtiny85) |
| `tfw_hal_i2c_init`, `_xfer` | SoC | SBCon bit-bang | LPI2C |
| `tfw_hal_spi_init`, `_xfer` | SoC | PL022 | LPSPI |
| `tfw_hal_ow_reset`, `_write`, `_read` | SoC | not linked | RGPIO and `tfw_hal_delay_us` |
| `tfw_hal_free_ram` | core | the stack pointer less the end of `.bss`, from linker symbols | the same |

"Not linked" means the capability's `.c` file is left out of the object list for
that board. The HAL symbol is then never referenced, and the board's CAPS page
says the capability is absent.

The boot HAL (`boot/boot_hal.h`), on the QEMU board only:

| `boot_hal.h` function | `mps2-an500` |
|---|---|
| `tfw_boot_flash_read`, `_write_page`, `_page_size` | memory copy over the application slot in PSRAM; the page size is the profile's `TFW_OTA_PAGE_MAX` |
| `tfw_boot_nvm_read`, `_write` | the platform block at the top of PSRAM |
| `tfw_boot_uart_init`, `_rx`, `_tx` | CMSDK UART0, polled |
| `tfw_boot_handoff_write`, `_read` | the `.handoff` section |
| `tfw_boot_reset_cause`, `tfw_boot_reset` | as the application HAL |
| `tfw_boot_jump_application` | mask interrupts, reset the used peripherals, set `VTOR` to the slot, load `MSP` from its first word, branch to its reset vector |

`tfw_boot_jump_application` cannot "jump to address 0" on Cortex-M the way it
does on AVR. The boot HAL contract already leaves *how* to the architecture, so
the header does not change.

### 5. Placement on the QEMU board, and the one QEMU behaviour it is built around

QEMU copies every image it loaded (`-kernel`, `-device loader`) back into memory
on **every** system reset, `SYSRESETREQ` included. A write over a loaded region
therefore does not survive a reset. RAM that QEMU did not load keeps its
contents across a system reset for the life of the process. *Confirm* this
against `hw/core/loader.c` in the implementing change.

The map follows from that:

| Region | Address | Holds | Why there |
|---|---|---|---|
| tfw-boot | SSRAM1 `0x00000000`, 32 KiB | `-kernel` image | reloaded on each reset, which is what a ROM or a locked boot section does |
| Application slot | PSRAM `0x60000000` | written by OTA through the boot HAL | not loaded by QEMU, so an OTA write survives `SYSRESETREQ` |
| Platform block | top 4 KiB of PSRAM | written by tfw-boot only | as above |
| SRAM | SSRAM2 `0x20000000`, `RAM_END` at the RAM budget | `.data`, `.bss`, stack, handoff | the RAM budget is enforced by the linker map, as on AVR |

As a result, the QEMU integration run exercises tfw-boot's whole loop on every
CI run:

1. boot with an empty slot;
2. recover;
3. OTA the image over UART0;
4. `SYSRESETREQ`;
5. verify and measure;
6. hand off;
7. jump;
8. run the protocol assertions.

Tests that do not need tfw-boot load the application alone with `-kernel` at
`0x60000000`.

### 6. Toolchain

| | Debian trixie package | Version |
|---|---|---|
| Compiler | `gcc-arm-none-eabi` | 15:14.2.rel1-1 (GCC 14.2, the same GCC as `gcc-avr`'s 14.2.0) |
| Binutils | `binutils-arm-none-eabi` | 23 (Debian's packaging revision of upstream binutils) |
| C library | `libnewlib-arm-none-eabi` (source package `newlib`) | 4.5.0.20241231-1 |
| Emulator | `qemu-system-arm` | 1:10.0.13+ds-0+deb13u1 (upstream 10.0) |

`mk/arch/cortex-m.mk`:

- `ARCH_CC := arm-none-eabi-gcc`, with `ARCH_OBJCOPY` and `ARCH_SIZE` to match,
  and `ARCH_LIBC := newlib-nano`.
- `ARCH_MACH_CFLAGS := -mcpu=$(CPU) -mthumb -mfloat-abi=soft`.
  - The firmware has no floating point today.
  - A soft-float ABI means no `CPACR` set-up, no lazy FPU stacking in ISRs, and
    one newlib multilib per CPU.
  - If a capability ever needs the FPU, this flag changes to `-mfpu=fpv5-d16`
    for the M7 and `fpv5-sp-d16` for the M33.
- `ARCH_OPT_CFLAGS := -Os -flto -ffunction-sections -fdata-sections -fno-fat-lto-objects`,
  as on AVR.
- `ARCH_LDFLAGS := -mcpu=$(CPU) -mthumb -specs=nano.specs -specs=nosys.specs -nostartfiles -Wl,--gc-sections`.
  The vector table and reset handler are ours, in `src/hal/cortex-m/`.
- `ARCH_FW_PLACEMENT_LDFLAGS` and `ARCH_BOOT_PLACEMENT_LDFLAGS`:
  - a per-SoC linker script from `mk/arch/cortex-m/<soc>.ld`;
  - the same `RAM_END - 79` formula for `.handoff` as `avr.mk`, without AVR's
    `0x800000` offset.
- `ARCH_FLASH`:
  - **QEMU board:** the recipe runs QEMU.
  - **i.MX 95:** the recipe stops and says the image ships inside
    trusted-imx95's container. This repository does not sign or write AHAB
    containers.

### 7. Budgets

These are policy gates, as `FLASH_BUDGET` and `RAM_BUDGET` are on AVR. They are
not the silicon's size.

| Board | Code budget | RAM budget | Region |
|---|---|---|---|
| `mps2-an500` | 131 072 (128 KiB) | 32 768 (32 KiB) | application slot in PSRAM; RAM in SSRAM2 |
| `mps2-an500` tfw-boot | 32 768 (32 KiB) | 8 192 (8 KiB) | SSRAM1 |
| `frdm-imx95-m7` | 131 072 (128 KiB) *confirm* | 32 768 (32 KiB) *confirm* | the M7's TCM; the split comes from IMX95RM and the SM configuration |

- **The figures are a ceiling, not a target.** They are about twice the
  ATmega2560's 64 KiB and 1 KiB, to leave room for 32-bit code, larger stacks
  and the soft crypto tier.
- **The QEMU board carries the i.MX 95 figures.** A build that fits the
  emulator therefore fits the reference part.
- **The i.MX 95 figures must be checked** against the M7's TCM size in IMX95RM
  and the M7 logical machine in the SM configuration (open question 2).

### 8. The boot chain on the i.MX 95

```
power-on
  └─ Boot ROM + ELE ............ NXP: AHAB authenticates the boot container
                                 against the SRK hash in fuses
       └─ System Manager (M33) .. NXP code, configured by trusted-imx95:
                                 TRDC, clocks, power, pins; starts the M7
                                 logical machine with its image in TCM
            └─ tfw firmware (M7) . this repository: the application, no tfw-boot
```

- **L0 is NXP's.** The ROM, the ELE and the SM are the measured and verified
  boot. That is the "ROM CDI tier replaces tfw-boot" case
  `multi-arch/architecture.md` §6 already anticipates, and the ATtiny85's
  "no boot section" shape.
- **The M7 starts from the AHAB container**, before and independent of the
  A55's Linux. This is what `hsm-self-defence` needs.
- **The M7 image is signed into trusted-imx95's container.** OTA of the M7
  firmware is therefore an update of that container (open question 5).
- **The CDI source on the i.MX 95 is open (#15).** It is either a derivation in
  the ELE keyed by the measured M7 image, or something else the ELE offers. The
  DICE certificate profile itself stays go-xkms's.

## Sources

The QEMU source and the Debian versions were read in this session:

- QEMU `hw/arm/mps2.c`, v10.0.0:
  <https://gitlab.com/qemu-project/qemu/-/raw/v10.0.0/hw/arm/mps2.c>
- QEMU `hw/arm/mps2-tz.c`, v10.0.0 (AN505, AN521):
  <https://gitlab.com/qemu-project/qemu/-/raw/v10.0.0/hw/arm/mps2-tz.c>
- Debian trixie package versions: <https://sources.debian.org/api/src/gcc-arm-none-eabi/>,
  <https://sources.debian.org/api/src/binutils-arm-none-eabi/>,
  <https://sources.debian.org/api/src/newlib/>,
  <https://sources.debian.org/api/src/qemu/>
- NXP System Manager source: <https://github.com/nxp-imx/imx-sm>

The documents below are cited by number. The lane could not fetch them:
developer.arm.com answered 403 to automated requests, and the NXP links need a
login or answered 404. Every *confirm* above traces to one of them.

| Number | Title | Where |
|---|---|---|
| IMX95RM | i.MX 95 Applications Processor Reference Manual (TCM sizes, M7 memory map, TRDC) | NXP, registration required: <https://www.nxp.com/products/i.MX95> |
| IMX95SRM | i.MX 95 Security Reference Manual (ELE, AHAB, key derivation) | NXP, NDA |
| IMX95IEC / IMX95AEC | i.MX 95 data sheets (industrial, automotive) | <https://www.nxp.com/products/i.MX95> |
| AN12312 | Secure Boot on i.MX 8 and i.MX 8X Families using AHAB (the AHAB container format i.MX 9 inherits) | <https://www.nxp.com/docs/en/application-note/AN12312.pdf> |
| DDI 0403 | Armv7-M Architecture Reference Manual | <https://developer.arm.com/documentation/ddi0403/latest/> |
| DDI 0553 | Armv8-M Architecture Reference Manual | <https://developer.arm.com/documentation/ddi0553/latest/> |
| DDI 0489 | Arm Cortex-M7 Processor Technical Reference Manual | <https://developer.arm.com/documentation/ddi0489/latest/> |
| DUI 0646 | Cortex-M7 Devices Generic User Guide | <https://developer.arm.com/documentation/dui0646/latest/> |
| 100230 | Arm Cortex-M33 Processor Technical Reference Manual | <https://developer.arm.com/documentation/100230/latest/> |
| DAI 0500 | Application Note AN500: Cortex-M7 SMM on V2M-MPS2 | <https://developer.arm.com/documentation/dai0500/latest/> |
| DAI 0505 / DAI 0521 | AN505: Cortex-M33 with the IoT Kit; AN521: dual Cortex-M33 with the SSE-200 subsystem; both on V2M-MPS2+ | <https://developer.arm.com/documentation/dai0505/latest/>, <https://developer.arm.com/documentation/dai0521/latest/> |

## Open questions for the owner

1. **Does an emulation board take a board id?** This ADR proposes `0x40` for
   `mps2-an500`, following the `esp32-qemu` precedent, and `0x41` for
   `frdm-imx95-m7`. Ids are allocated in the change that adds the board, not
   here.
2. **The i.MX 95 budgets.** The 128 KiB code and 32 KiB RAM figures need
   checking against the M7 TCM size in IMX95RM, which the lane could not read,
   and against the TCM the SM configuration gives the M7.
3. **The M7's console.** Which LPUART, and which pins, the SM configuration
   assigns to the M7 logical machine on the FRDM-IMX95. That is trusted-imx95's
   configuration, so the question is a dependency on that repository.
4. **The platform block on the i.MX 95.** There are three candidates:
   - a read-only block inside the signed container;
   - ELE or fuse data;
   - SM-managed storage.

   Only tfw-boot writes it today, and the i.MX 95 has no tfw-boot.
5. **M7 OTA.** Is the M7 image updated only as part of trusted-imx95's
   container? Or does the M7 get A/B slots and its own tfw-boot later?
6. **Reset causes.** `tfw_reset_cause_t` has no `SOFTWARE` and no `LOCKUP`.
   Both are common on Cortex-M. Appending them (`5`, `6`) is an extend-only wire
   change for its own PR. Until then they read as `UNKNOWN`.
7. **The MPU in the first port.** This ADR leaves it out of the first port.
   Should stack-overflow and null-pointer guard regions be in the first slice,
   or follow it?
8. **The soft-float ABI** (decision 6). It is cheap to keep and costs one flag
   to change later.
9. **When the M33 target lands.** Does QEMU `mps2-an505` land with the STM32H5
   port, or with #15?

## Rejected alternatives

| Alternative | Why not |
|---|---|
| An M33 part first (STM32H5) | Against the ruling. It also puts the TrustZone and TF-M decision (#15) on the critical path of the first Cortex-M image. |
| QEMU `mps3-an547` | A Cortex-M55: Armv8.1-M with Helium. That is the wrong ISA for both the M7 and the M33. |
| QEMU `netduinoplus2`, `olimex-stm32-h405` | STM32F405, a Cortex-M4: neither an M7 nor TrustZone-M. |
| QEMU `lm3s6965evb`, `lm3s811evb`, `mps2-an385`, `mps2-an511` | Cortex-M3: no Armv7E-M, no FPU, and no part we target. |
| An i.MX model in QEMU | None exists for the i.MX 95 or its M7. |
| Renode | It models more SoCs, but it is not in Debian. It would put a .NET runtime and an upstream binary into the CI trust chain that `docker/packages.txt` keeps Debian-only. |
| No emulation: host build and mock only | That is the unit tier, which already exists. The integration tier exists to run the image that ships: the vector table, the linker map and the ISRs. |
| A vtable or `#ifdef CORTEX_M7` in the core | It breaks link-time substitution and the `#if` confinement rule in CLAUDE.md. |

## Follow-ups (not in this ADR)

- **The lint gates.**
  - `lint-arch-leak` matches AVR patterns only. A Cortex-M leak pattern
    (`<cmsis`, `__DSB`, `SCB->`) should be added with the first Cortex-M code.
  - The cppcheck call in `mk/host.mk` hard-codes `-Isrc/hal/avr`.
- **`multi-arch/architecture.md`.**
  - §6 still names the LPC55S69, MCXN and STM32U5 as the Cortex-M33 roots of
    trust.
  - §7 has no Cortex-M7 row.

  Both predate the 19:48Z ruling and need a docs pass.
- **trusted-imx95.** It must provide the M7 logical machine, its console and
  the M7 slot in the boot container. This is reported as a dependency, not
  worked around here.
