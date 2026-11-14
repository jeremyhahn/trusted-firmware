# ADR: the Cortex-M reference part and its CI emulation target

> **Status: ACCEPTED (owner ruling 632 item 208 a, 2026-11-14)** · Issue: #14 · SPEC rows: `cortex-m` (future),
> `imx95`, `stm32h5`, `nrf54l15` · Scope: trusted-firmware only · Last verified: 2026-11-14
>
> Documentation only. No code, build file, board id or SPEC row changes with this
> ADR. Every number marked *confirm* comes from a document that could not be read
> (named where it is used), and is a starting point for the owner, not a fact.


## Accepted on 2026-11-14 (owner ruling 632 item 208 a)

The owner ruled on 2026-11-14 (ruling 632, item 208 a), verbatim:

> "ADRs #14 and #15 adopt the i.MX 95 M7 reference part, QEMU mps2-an505
> emulation, TF-M Profile Medium plus our DICE attestation partition on the M33
> secure world, and MPU-only isolation on the M7. Record it as one PR."

What the ruling decides in this ADR:

| Decision | Now |
|---|---|
| 1. Reference part | **decided**: the i.MX 95 Cortex-M7 is the reference part, checked against the STM32H573I-DK, the nRF54L15-DK and `mps2-an505`. Question 1 is answered (a): the M7, not an M33 |
| 3. CI emulation | **decided**: the CI emulation target is QEMU `mps2-an505`. Question 2 is answered for the target; whether `mps2-an500` joins it is question 8, still open |
| 2, 4 to 8 | accepted as written; the ruling does not name them, and the open questions on them stay open |

The trust model the ruling picks for the M33 and the M7 is recorded in #15
([`adr-trust-model-vs-tf-m.md`](adr-trust-model-vs-tf-m.md)), accepted on the
same ruling. Questions 3 to 10 below are not answered by it and stay open.

## Correction on 2026-10-20

Decision 1 is corrected. The 2026-10-18 revision made the Cortex-M33 the
reference part and gave CA m623 as the reason, but CA m623 does not move the
reference part, and the SPEC row `cortex-m` still names the i.MX 95's Cortex-M7.
Decision 1 records the M7 as the reference part again. The two M33 bench
boards are the parts it is checked against, and `mps2-an505` stays the CI
target. Question 1 now asks the owner to choose between the two. Decisions 2 to
8 are unchanged.

## Changes on 2026-10-18 (owner, CA m623)

The owner ruled on 2026-10-18 (CA m623, 12:35Z) that this ADR is no longer gated
on the i.MX 95, and that it is decided against the Cortex-M parts on the bench
host: the STM32H573I-DK and the nRF54L15-DK, both Cortex-M33 with TrustZone-M.
The generic `cortex-m` port runs on QEMU `mps2-an505` in CI. The hardware order
stays the SE052F, then the i.MX 95 M7, then the STM32H5, and the STM32H5 port
starts only once the ESP32-P4 has proven the composed vendor-boot model (#79).

| Decision | What changed |
|---|---|
| 1. Reference part | decided against the two bench boards, the STM32H573I-DK and the nRF54L15-DK. This revision also made the Cortex-M33 the reference part, which the ruling does not say; corrected 2026-10-20 (see above) |
| 2. Layout | the QEMU board is `mps2-an505` (`CPU := cortex-m33`), not `mps2-an500` |
| 3. CI emulation | `mps2-an505` is the CI target now, not "the M33 later". `mps2-an500` is no longer in CI; it is kept only as an option for the M7 (question 8) |
| 4. HAL table | the QEMU column is `mps2-an505`; the M7 column is unchanged |
| 5. QEMU placement | the map is on `mps2-an505`'s SSRAM, not `mps2-an500`'s PSRAM |
| 7. Budgets | the QEMU board carries the smallest board's figures; the M7 TCM size is now known for the family |
| 6, 8 | unchanged |

Each decision below carries the ruling where it changed. The onboarding ADRs
for the two bench boards are
[`../multi-arch/adr-stm32h573i-dk-onboarding.md`](../multi-arch/adr-stm32h573i-dk-onboarding.md)
(#126) and
[`../multi-arch/adr-nrf54l15-dk-onboarding.md`](../multi-arch/adr-nrf54l15-dk-onboarding.md)
(#129).

## The question

The SPEC row `cortex-m` says the Cortex-M ports are generic architecture ports,
Cortex-M7 and Cortex-M33 first, each a HAL and a boot-HAL port, with boards as
data. The i.MX 95's Cortex-M7 is a board. Until a board port lands, the Cortex-M
targets run in QEMU.

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
  - The CDI handoff block is 112 bytes (version 4; 80 before #173) at `RAM_END - 111`, and both images place
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

### 1. The reference part: the i.MX 95 Cortex-M7, checked against the two M33 boards

**Decided 2026-11-14 (owner ruling 632 item 208 a): the i.MX 95 M7 is the
reference part.** Question 1 is answered (a).

*Corrected 2026-10-20.* The 2026-10-18 revision of this decision made the
Cortex-M33 the reference part and gave CA m623 as the reason. The ruling does
not say that. It decides this ADR against the boards present, puts the generic
port on `mps2-an505` in CI and keeps the hardware order. The SPEC row `cortex-m`
still names the i.MX 95's Cortex-M7 as the reference part and the STM32H5 as the
second target. This decision records the SPEC's reference part. Making the M33
the reference part instead is option (b) of question 1, for the owner.

The **reference part is the Cortex-M7 of the NXP i.MX 95** on the NXP
FRDM-IMX95-PRO (UM12527; SPEC rows `cortex-m` and `imx95`, #49). As CA m623
rules, the ADR is decided against the Cortex-M parts on the bench host. Both are
Cortex-M33s with TrustZone-M, and both are the next targets:

- the **STM32H573I-DK** (SPEC row `stm32h5`), the second target in the SPEC: an
  upstream TF-M platform, with Ethernet from the MCU, so it enrols as the AVR
  boards do;
- the **nRF54L15-DK** (SPEC row `nrf54l15`): no upstream TF-M port, RRAM rather
  than flash, and no Ethernet. Its port comes after the generic HAL (#161).

In CI the core is QEMU `mps2-an505`, also an M33 (decision 3). "Reference"
names the part whose constraints the core HAL must meet. It does not set the
order in which boards are brought up. That order is the owner's and stays as
ruled: the SE052F, then the i.MX 95 M7, then the STM32H5, which waits for #79.

| | i.MX 95 Cortex-M7 (reference part) | STM32H573I-DK (second target) | nRF54L15-DK (bench board) |
|---|---|---|---|
| Architecture | Armv7E-M, no Security Extension | Armv8-M Mainline with the Security Extension | the same |
| Where the code runs | TCM, loaded from the AHAB container; no on-chip flash | on-chip flash, executed in place | on-chip RRAM, executed in place |
| Root of trust | ROM + ELE: AHAB verifies the container against the fused SRK hash | ST's boot ROM (STiRoT or OEMiRoT); #126 | an immutable boot region in RRAM; #129 |
| Where keys live | ELE, outside the core | the secure world (#15) | the secure world, or a KMU slot (#15) |
| Isolation from other masters | SoC: TRDC configured by the SM; a separate core from the A55 | TrustZone-M (SAU/IDAU), with the flash and SRAM attribution set by the secure world | TrustZone-M, with peripherals assigned to either world |
| tfw-boot | none; L0 is NXP's ROM, ELE and SM | depends on #15's model | depends on #15's model |
| Link to the host | an LPUART the SM assigns | Ethernet, and the STLINK virtual COM port | the J-Link virtual COM port |

Where the DICE root lives on each of them is #15's ("Where the DICE root lives,
per part").

Why the M7 is the reference part:

- **The SPEC rows `cortex-m` and `imx95` name it**, and the M7 is the core that
  `hsm-self-defence` needs.
- **It is the most constrained of the three.** It has no on-chip flash, no
  tfw-boot, no Security Extension, and a root of trust on other silicon. A core
  HAL that meets the M7 meets the M33 boards, run in one security state
  (decision 3).
- **Its trust model is closest to what works today.** The keys stay in a
  separate element, the ELE, as on the AVR element tier, and the firmware only
  asks for operations.

Why the core HAL is still checked on the M33:

- **Both bench parts are M33s**, and the M7 image ships inside trusted-imx95's
  container. Until trusted-imx95 places it there, the M7 is checked by the
  board-sweep build alone (question 8).
- **`mps2-an505` models the M33 and its security controllers**, so CI proves the
  same core the bench boards carry (decision 3).
- **The core HAL carries over between the two.** SysTick, NVIC, SCB, the MPU and
  the vector table are architecturally the same in Armv7-M and in each security
  state of Armv8-M Mainline (Armv7-M Architecture Reference Manual, DDI 0403,
  chapter B3 "System Address Map", not fetched, so this half is to confirm;
  Armv8-M Architecture Reference Manual, DDI 0553 issue B.z, its system address
  map chapter, listed in [`../reference/README.md`](../reference/README.md#cortex-m-sources)). Only the per-CPU flags and the SoC files differ.

What it costs:

- **The secure/non-secure split is #15's.** Until #15 is accepted, the QEMU
  image runs in the Secure state alone, as a part with no secure world would.
  That is a CI configuration, not a trust model: no Cortex-M board holds a
  DeviceID key until #15 is accepted.
- **The M7 board still depends on trusted-imx95.** That repository must assign
  the M7 a logical machine in the SM configuration, and place the M7 image in
  the boot container. This is a dependency to report, not work to do here.
- **The reference part runs no tfw-boot.** The first tfw-boot on Cortex-M is the
  QEMU board (decision 5); on the M33 boards it is #15's to decide.
- **No QEMU model of any of the three exists.** QEMU has no i.MX 95, no STM32H5
  and no nRF54L15. CI therefore proves the core and the protocol, never the SoC
  (decision 3).

### 2. One architecture, `cortex-m`; CPUs and SoCs are data

```
mk/arch/cortex-m.mk          ARCH := cortex-m; picks -mcpu/-mfpu by $(CPU)
src/hal/cortex-m/            the core: shared by every Cortex-M part
src/hal/cortex-m/mps2/       CMSDK UART, FPGAIO, PL022, SBCon (QEMU board)
src/hal/cortex-m/imx95/      LPUART, RGPIO, LPI2C, LPSPI, TPM, SCMI-over-MU client
boot/hal/cortex-m/           the boot HAL core
boot/hal/cortex-m/mps2/      pseudo-flash and NVM on MPS2 RAM (decision 5)
boards/mps2-an505/           profile.mk: ARCH := cortex-m, CPU := cortex-m33, SOC := mps2
boards/frdm-imx95-m7/        profile.mk: ARCH := cortex-m, CPU := cortex-m7, SOC := imx95
```

- `profile.mk` gains `CPU` and `SOC` beside `ARCH`.
- `mk/arch/cortex-m.mk` adds `src/hal/cortex-m/$(SOC)/*.c` to the HAL object
  list. Selection is by make, and the C has no `#if`.
- Every other board reuses `src/hal/cortex-m/` unchanged. It adds its SoC
  directory and sets its `CPU`: `cortex-m33` for the two bench boards,
  `cortex-m7` for the i.MX 95.
- The secure world, if we write one, is #15's design, not this layout's.

### 3. The CI emulation target: QEMU `mps2-an505`

**Decided 2026-11-14 (owner ruling 632 item 208 a): the CI emulation is QEMU
`mps2-an505`.**

*Changed 2026-10-18 (owner, CA m623).* The CI target was QEMU `mps2-an500`, a
Cortex-M7, with `mps2-an505` "for the M33 later". The owner ruled that the
generic port runs on `mps2-an505` in CI, so that text is superseded.

The CI target is **QEMU `mps2-an505`**: the Arm MPS2 board with the AN505 FPGA
image, a single Cortex-M33 with the Security Extension, on Arm's IoT Kit
subsystem (Arm Application Note AN505, DAI 0505, issue B, listed in
[`../reference/README.md`](../reference/README.md#cortex-m-sources)). It is what TF-M
and Zephyr test the M33 against.

The facts below are read from QEMU `hw/arm/mps2-tz.c` at v10.0.0
(<https://gitlab.com/qemu-project/qemu/-/raw/v10.0.0/hw/arm/mps2-tz.c>), which is the
version Debian trixie ships.

**Security.** The core resets in the Secure state with its Secure vector table
at `0x10000000`. The IDAU marks an address Secure when bit 28 is set, so every
region has a Non-secure alias and a Secure alias `0x10000000` above it. Three
memory protection controllers (MPCs) and the peripheral protection controllers
(PPCs) gate each region and peripheral; the Secure world configures them.

**Memory.** Everything is RAM. The board has no flash controller.

| Region | Non-secure address | Size |
|---|---|---|
| SSRAM-0 | `0x00000000` | 4 MiB, aliased at `0x00400000` |
| SSRAM-1 | `0x28000000` | 2 MiB |
| SSRAM-2 | `0x28200000` | 2 MiB |

**Clocks.** SYSCLK is 20 MHz.

**Interrupts.** The IoT Kit takes the first 32 interrupt lines, and the board
adds 92 more from 32 up. Every interrupt number in this section is an **NVIC
line**: the M33's external interrupt input IRQ[n], enabled through
`NVIC_ISER[n / 32]`, exception number 16 + n. It is not a number local to the
expansion. AN505 (DAI 0505B, chapter 7 "Interrupt Map", Table 7-1 "FPGA
Expansion Interrupt Map") numbers the expansion's interrupts as M33 inputs:
IRQ[0]–IRQ[12] are the IoT Kit's, IRQ[31:13] are reserved, and UART0's RX and
TX are IRQ[32] and IRQ[33]. Its §7.1 "UARTs Interrupts" says each UART
interrupt "drive[s] a single interrupt input of the M33 CPU". That agrees with
`boards/mps2-an505/board_profile.h`, whose `TFW_MPS2_UART0_IRQ_A` and `_B`,
read by `src/hal/cortex-m/mps2/soc_regs.h`, are external interrupts 32 and 33
([`../reference/README.md`](../reference/README.md#cortex-m-sources)).

**Peripherals** (Non-secure addresses).

| Peripheral | Address | Interrupt (NVIC line, AN505 Table 7-1) | Notes |
|---|---|---|---|
| CMSDK APB UART ×5 | `0x40200000`–`0x40204000` | UART0: RX on 32, TX on 33 | UART0 is the first `-serial` |
| PL022 SPI ×5 | `0x40205000`, `0x40206000`, `0x40209000`, `0x4020a000`, `0x4020b000` | 51–55 | |
| SBCon bit-banged I2C ×4 | `0x40207000`, `0x40208000`, `0x4020c000`, `0x4020d000` | | the last two are the shield buses |
| FPGAIO | `0x40302000` | | two LEDs, no switches |
| LAN9118 Ethernet | `0x42000000` | 48 | |

The CMSDK GPIO blocks at `0x40100000`–`0x40103000` are *unimplemented devices*.
They read as zero and ignore writes. The timers and the watchdogs are the IoT
Kit's; their addresses are read in the implementing change.

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
- **The MPU (PMSAv8)**, if the port uses one (see question 5).
- **TrustZone-M, once #15 is accepted:** the SAU and IDAU attribution, the MPC
  and PPC set-up, and the Non-secure-callable entries #15 chooses.
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
| The STM32H573's and the nRF54L15's own peripherals, boot ROMs, key stores and option bytes or RRAM protection | the bench, on the STM32H573I-DK and the nRF54L15-DK |
| The i.MX 95 itself: ELE, AHAB, SM, SCMI, MU, TRDC, LPUART, LPI2C, LPSPI, TPM | the bench on the NXP FRDM-IMX95-PRO (UM12527), and trusted-imx95's own tests |
| Caches, TCM timing, bus contention, cycle-accurate timing | the bench, once a board port lands |
| Real interrupt latency under load | the bench |

The two FPGAIO LEDs give the board a few real output pins, so the GPIO wire
path has something to write to; reads come back from the LEDs' own state. The SoC capability files for ADC, PWM, tone,
pulse, one-wire and EEPROM are left out of the object list for `mps2-an505`, so
those capabilities report `TFW_ERR_UNSUPPORTED`. They do not pretend.

**How it runs in `make ci` without a skip guard:**

- **Packages.** `docker/packages.txt` gains `gcc-arm-none-eabi`,
  `binutils-arm-none-eabi`, `libnewlib-arm-none-eabi` and `qemu-system-arm`.
  Their versions are recorded in `/etc/ci-toolchain.txt` the way avr-gcc's are.
- **The target.** A new `integration-test-cortex-m` target lives in its own
  `mk/integration-cortex-m.mk`, and `ci:` lists it after
  `integration-test-boot`.
  - It builds `BOARD=mps2-an505`.
  - It runs a harness in `test/integration/qemu/`.
  - The harness speaks the same frames and makes the same assertions as the
    simavr harness, through the same `src/proto/crc16.c`.
- **The QEMU command.**
  `qemu-system-arm -M mps2-an505 -nographic -icount shift=0,sleep=off -kernel <elf>`.
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

**`mps2-an500`, kept for the M7 only.** QEMU `mps2-an500` (`hw/arm/mps2.c` at
v10.0.0; Arm AN500, DAI 0500) is the only Cortex-M7 machine in QEMU 10.0. It
was this decision's CI target until 2026-10-18. It is no longer in CI. Whether a
second CI board builds and runs the image with `CPU := cortex-m7`, so that the
M7's flags are proven before the i.MX 95 port, is question 8. `mps2-an521` is
the dual-core AN505 (AN521, DAI 0521) and adds nothing a single-core port needs.

### 4. The HAL, function by function

"Core" means `src/hal/cortex-m/`. The two right-hand columns are the SoC files.

| `hal.h` function | Where | `mps2-an505` (QEMU) | `frdm-imx95-m7` |
|---|---|---|---|
| `tfw_hal_init` | core + SoC | SysTick; UART0; FPGAIO | SysTick; LPUART; clocks and pins requested from the SM over SCMI |
| `tfw_hal_reset_cause` | SoC | a `.noinit` marker set before `SYSRESETREQ`; otherwise `POWER_ON` | the SM's reset reason for the M7 logical machine, over SCMI |
| `tfw_hal_reset` | core | `AIRCR.SYSRESETREQ` | an SCMI logical-machine reset request; what it resets is SM policy |
| `tfw_hal_irq_enable` | core | `cpsie i` | `cpsie i` |
| `tfw_hal_millis` | core | SysTick at 1 kHz | SysTick at 1 kHz |
| `tfw_hal_delay_us` | core | busy-poll of SysTick `VAL` (no DWT: QEMU does not model `CYCCNT`) | the same |
| `tfw_hal_uart_init`, `_rx`, `_tx_try`, `_tx_idle` | SoC | CMSDK UART0, interrupt-driven RX | the LPUART the SM assigns to the M7 (question 7) |
| `tfw_hal_gpio_mode`, `_write`, `_read` | SoC | FPGAIO LEDs (out; a read returns the LED state) | RGPIO; pin mux from the SM |
| `tfw_hal_adc_init`, `_set_ref`, `_read` | SoC | not linked | SAR ADC, if the SM assigns it |
| `tfw_hal_pwm_write`, `_stop` | SoC | not linked | TPM |
| `tfw_hal_tone_start`, `_stop` | SoC | not linked | TPM |
| `tfw_hal_pulse_in`, `tfw_hal_pulse_capture` | SoC | not linked | TPM input capture |
| `tfw_hal_eeprom_read`, `_write` | — | not linked; `TFW_EEPROM_SIZE 0` | not linked; `TFW_EEPROM_SIZE 0` |
| `tfw_hal_nvm_read` | SoC | the platform block in SSRAM-0 (decision 5) | question 7 |
| `tfw_hal_handoff_block` | core | the linker-placed `.handoff` at `RAM_END - 111` | none: no tfw-boot, so no handoff (as the ATtiny85) |
| `tfw_hal_i2c_init`, `_xfer` | SoC | SBCon bit-bang | LPI2C |
| `tfw_hal_spi_init`, `_xfer` | SoC | PL022 | LPSPI |
| `tfw_hal_ow_reset`, `_write`, `_read` | SoC | not linked | RGPIO and `tfw_hal_delay_us` |
| `tfw_hal_free_ram` | core | the stack pointer less the end of `.bss`, from linker symbols | the same |

"Not linked" means the capability's `.c` file is left out of the object list for
that board. The HAL symbol is then never referenced, and the board's CAPS page
says the capability is absent.

The boot HAL (`boot/boot_hal.h`), on the QEMU board only:

| `boot_hal.h` function | `mps2-an505` |
|---|---|
| `tfw_boot_flash_read`, `_write_page`, `_page_size` | memory copy over the application slot in SSRAM-0; the page size is the profile's `TFW_OTA_PAGE_MAX` |
| `tfw_boot_nvm_read`, `_write` | the platform block at the top of SSRAM-0 |
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

*Changed 2026-10-18 (owner, CA m623):* the map was on `mps2-an500`'s SSRAM1
and PSRAM. On `mps2-an505` it follows the same rule. The addresses are the
Secure aliases, because the image runs Secure until #15 is accepted, and each
is to *confirm* in the implementing change:

| Region | Address | Holds | Why there |
|---|---|---|---|
| tfw-boot | SSRAM-0 `0x10000000`, 32 KiB | `-kernel` image; the Secure vector table QEMU resets to | reloaded on each reset, which is what a ROM or a locked boot section does |
| Application slot | SSRAM-0 `0x10100000` (1 MiB up) | written by OTA through the boot HAL | not loaded by QEMU, so an OTA write survives `SYSRESETREQ` |
| Platform block | top 4 KiB of SSRAM-0 | written by tfw-boot only | as above |
| SRAM | SSRAM-1 `0x38000000`, `RAM_END` at the RAM budget | `.data`, `.bss`, stack, handoff | the RAM budget is enforced by the linker map, as on AVR |

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
the application slot.

### 6. Toolchain

The versions are from the Debian sources API for trixie
(<https://sources.debian.org/api/src/gcc-arm-none-eabi/>, `binutils-arm-none-eabi/`,
`newlib/` and `qemu/` under the same path).

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
  The stack top is 8-byte aligned, as the AAPCS requires at a public
  interface (IHI 0042, 2025Q4, "The Stack" › "Stack constraints at a public
  interface"; copied in [`../reference/aapcs32-2025Q4.rst`](../reference/aapcs32-2025Q4.rst)).
- `ARCH_FW_PLACEMENT_LDFLAGS` and `ARCH_BOOT_PLACEMENT_LDFLAGS`:
  - a per-SoC linker script from `mk/arch/cortex-m/<soc>.ld`;
  - the same `RAM_END - 111` formula for `.handoff` as `avr.mk`, without AVR's
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
| `mps2-an505` | 131 072 (128 KiB) | 32 768 (32 KiB) | application slot in SSRAM-0; RAM in SSRAM-1 |
| `mps2-an505` tfw-boot | 32 768 (32 KiB) | 8 192 (8 KiB) | SSRAM-0 |
| `frdm-imx95-m7` | 131 072 (128 KiB) *confirm* | 32 768 (32 KiB) *confirm* | the M7's TCM; the split comes from IMX95RM and the SM configuration |

- **The figures are a ceiling, not a target.** They are about twice the
  ATmega2560's 64 KiB and 1 KiB, to leave room for 32-bit code, larger stacks
  and the soft crypto tier.
- **The QEMU board carries the smallest board's figures.** *Changed 2026-10-18
  (owner, CA m623):* it carried the i.MX 95's, as the reference part's. It
  still does in size, since the two are equal; the reason is now the fit. The
  STM32H573 (2 MB flash, 640 KB SRAM) and the nRF54L15 (up to 1524 KB RRAM,
  256 KB RAM) are larger (onboarding ADRs #126 and #129, both *confirm*), so a
  build that fits the emulator fits every board. A secure image (#15) takes its
  share of each part outside these figures.
- **The i.MX 95 figures must be checked** against the M7's TCM. The family's M7
  has 512 KB of TCM (IMX95CEC Rev. 8, Table 1, as recorded in #15's question 12).
  The ITCM/DTCM split is in IMX95RM
  (the i.MX 95 Applications Processor Reference Manual, its M7 memory map;
  NXP, registration required, not read) and the M7 logical machine in the SM
  configuration (question 7).

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
  boot. The AHAB container format is the one NXP AN12312 describes for i.MX 8
  (<https://www.nxp.com/docs/en/application-note/AN12312.pdf>, not fetched;
  i.MX 9 inherits it), and the SM is NXP's `imx-sm`
  (<https://github.com/nxp-imx/imx-sm>). That is the "ROM CDI tier replaces tfw-boot" case
  `multi-arch/architecture.md` §6 already anticipates, and the ATtiny85's
  "no boot section" shape.
- **The M7 starts from the AHAB container**, before and independent of the
  A55's Linux. This is what `hsm-self-defence` needs.
- **The M7 image is signed into trusted-imx95's container.** OTA of the M7
  firmware is therefore an update of that container (question 9).
- **The CDI source on the i.MX 95 is open (#15).** It is either a derivation in
  the ELE keyed by the measured M7 image, or something else the ELE offers. The
  DICE certificate profile itself stays go-xkms's. What the ELE offers is in the
  i.MX 95 Security Reference Manual (IMX95SRM, its ELE key-derivation chapter;
  NXP, NDA, not read).

## Open questions for the owner

Each question is answered yes or no; a "no" sends the decision it names back
for a revision. The ADR was accepted on 2026-11-14 (owner ruling 632 item
208 a), which answers question 1 and the CI target of question 2. Questions 3
to 10 are not answered by the ruling and stay open.

1. **Closed 2026-11-14: (a), the i.MX 95 M7 (ruling 632 item 208 a).**
   **The reference part** (decision 1). (a) Does the i.MX 95 Cortex-M7 stay the
   reference part, as the SPEC row `cortex-m` has it, with the core HAL checked
   on the STM32H573I-DK, the nRF54L15-DK and `mps2-an505`? Or (b) does the
   Cortex-M33 with TrustZone-M become the reference part, with the
   STM32H573I-DK as the reference board and the i.MX 95 M7 a board of the port
   (#49)? A (b) changes the SPEC rows `cortex-m` and `stm32h5` in the same
   change. The ADR proposes (a), because the SPEC names it.
2. **Closed 2026-11-14 for the target: QEMU `mps2-an505` (ruling 632 item
   208 a).** Whether it stays the only Cortex-M machine is question 8, open.
   **The CI target** (decision 3). Is QEMU `mps2-an505` the only Cortex-M machine
   in `make ci`, with the image running in the Secure state alone until #15 is
   accepted?
3. **Board ids.** Does the emulation board take `0x40`, as `esp32-qemu` takes
   one, and do the STM32H573I-DK, the nRF54L15-DK and `frdm-imx95-m7` take the
   next free ids in `0x40`–`0x7F` in the order their ports land? Each id is
   allocated in the change that adds the board, not here.
4. **Reset causes.** `tfw_reset_cause_t` has no `SOFTWARE` and no `LOCKUP`, and
   both are common on Cortex-M. Are they appended as `5` and `6`, an extend-only
   wire change in its own PR? Until then they read as `UNKNOWN`.
5. **The MPU.** Does the MPU stay out of the first port and land, with
   stack-overflow and null-pointer guard regions, before any image links a
   key-backed attestation command (#15)?
6. **The soft-float ABI** (decision 6). Is `-mfloat-abi=soft` kept? It costs one
   flag to change later.
7. **The i.MX 95 board's open facts.** Are its console (which LPUART and pins the
   SM assigns the M7 on the NXP FRDM-IMX95-PRO, UM12527), its platform block (a
   read-only block in the signed container, ELE or fuse data, or SM-managed
   storage) and its TCM budgets deferred to the i.MX 95 board port (#49), with
   trusted-imx95 named as its dependency?
8. **`mps2-an500` for the M7.** Is `mps2-an500` added as a second CI board,
   built with `CPU := cortex-m7`, before the i.MX 95 port lands? A "no" leaves
   the M7 flags proven only by the board-sweep build and the bench.
9. **M7 OTA.** Does the M7 stay without an OTA command, its image updated only
   inside trusted-imx95's container, until #15 settles the anti-rollback floor
   and who stages the image? This carries the owner's partial answer of
   2026-10-15 (Q11: AHAB and the enclave verify the M7 container).
10. **The nRF54L15-DK's place in the order.** The hardware order is the SE052F,
    then the i.MX 95 M7, then the STM32H5 (CA m623). Does the nRF54L15-DK port
    wait behind the STM32H5, under the same #79 gate?

Answered since the last revision: "when does `mps2-an505` land" (with the
STM32H5 port, or with #15) is closed by CA m623: it is the CI target now.

## Rejected alternatives

| Alternative | Why not |
|---|---|
| The Cortex-M33 as the reference part | Not rejected: it is option (b) of question 1. The 2026-10-18 revision chose it, citing CA m623, and the ruling does not say so. Both bench parts are M33s, but the SPEC row names the M7 (correction of 2026-10-20). |
| QEMU `mps2-an500` as the CI target | Superseded 2026-10-18 (owner, CA m623): the CI target is `mps2-an505`. `mps2-an500` stays an option for the M7 only (question 8). |
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

  Both need a docs pass to match this ADR.
- **trusted-imx95.** It must provide the M7 logical machine, its console and
  the M7 slot in the boot container. This is reported as a dependency, not
  worked around here.
