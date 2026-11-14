# mps2-an500 (Arm AN500 under QEMU)

> **Status: INFORMATIVE** · SPEC row: `cortex-m` · Scope:
> `boards/mps2-an500/` · Last verified: 2026-11-12
>
> A summary of what the board files say, with each value cited to where the
> build reads it. The normative contract is [`adding-a-board.md`](adding-a-board.md);
> where this page and a board file disagree, the board file is right and this
> page is the defect.

Arm's MPS2+ FPGA image AN500, a Cortex-M7 (ARMv7-M, no TrustZone), as
`qemu-system-arm -M mps2-an500` models it. A test target, not a part: the
generic Cortex-M port's second CI target, so every change to the core boots on
an ARMv7-M M7 as well as the ARMv8-M M33 of `mps2-an505`
(`profile.mk:4`–`8`; `board_profile.h:4`–`10`). The decisions are
[`../cortex-m/README.md`](../cortex-m/README.md) and its two ADRs.

## The machine

| | Value | Source |
|---|---|---|
| Core | `cortex-m7`, FPU off, as every Cortex-M build (`mk/arch/cortex-m.mk`) | `profile.mk:10` |
| SoC directory | `mps2`, shared with `mps2-an505`: `src/hal/cortex-m/mps2/`, `boot/hal/cortex-m/mps2/`, `mk/arch/cortex-m/mps2.ld` | `profile.mk:11` |
| QEMU machine | `mps2-an500` | `profile.mk:14` |
| Clock | 25 MHz, QEMU's `SYSCLK_FRQ`; SysTick, the UART divisor and the watchdog count it | `board_profile.h:36`–`38` |
| UART0 | CMSDK APB UART at `0x40004000`; receive interrupt on external interrupt 0, transmit on 1 | `board_profile.h:40`–`45` |
| LEDs | the FPGA I/O block at `0x40028000`, LED register at +0, two user LEDs | `board_profile.h:46`; `board.c:9`–`20` |
| Watchdog | CMSDK APB watchdog at `0x40008000`, its interrupt wired to the NMI | `board_profile.h:40`–`42`, `:47` |

Sources for the addresses and interrupts: QEMU v10.0.0 `hw/arm/mps2.c` (the
`mps2-an500` machine: UART0 at `0x40004000` on IRQs 0 and 1, the watchdog at
`0x40008000` on the NMI, FPGAIO at `0x40028000`, `SYSCLK_FRQ` 25 MHz), and
Arm Application Note AN500, *Example Cortex-M7 SMM for MPS2*.

## The memory map

| Address | Region | Use | Source |
|---|---|---|---|
| `0x00000000` | ZBT SSRAM1, 4 MiB | tfw-boot, 32 KiB at the base, where VTOR points at reset (0 on an ARMv7-M core) | `profile.mk:16`–`21`, `:39`–`41`; `board_profile.h:53`–`54` |
| `0x00008000` | ZBT SSRAM1 | the application slot, 128 KiB | `profile.mk:28`, `:33`; `board_profile.h:55`–`56` |
| `0x003FF000` | ZBT SSRAM1 | the platform block, the last 4 KiB | `board_profile.h:59`–`63` |
| `0x20000000` | ZBT SSRAM2 and 3, 4 MiB | `.data`, `.bss`, the stack, the reset marker and the handoff block at the top (`RAM_END` `0x203FFFFF`) | `profile.mk:22`–`23`, `:34`–`35` |

The code region is RAM that QEMU loads from the raw images, so a write "to
flash" is a store, and a reset restores everything but the platform block, as
on `mps2-an505` (`profile.mk:25`–`27`). `TFW_FLASH_TOTAL` is the end of SSRAM1,
the span both regions sit in (`board_profile.h:57`).

## Board id

`0x42` (`board_profile.h:16`), in the Cortex-M range of the
[board-id registry](board-id-registry.md), and `tfw.BoardMPS2AN500` in
`sdk/go/boardid.go`.

## Pins in summary

| | Value | Source |
|---|---|---|
| Pins | 3: the two user LEDs, outputs only, and pin 2, the shield 0 SPI select; 4 in the `ONEWIRE=1` row, pin 3 the 1-Wire line | `board_profile.h`, `TFW_PIN_COUNT`; `board.c` |
| Ports | 2: the FPGA I/O block's LED register (bit n is LED n) and its MISC register; 3 in the `ONEWIRE=1` row, port 2 the CMSDK GPIO block 0 | `board_profile.h`, `TFW_PORT_COUNT`; `board.c` |
| PWM, ADC | none: QEMU models no converter or PWM this port drives | `board_profile.h` |
| EEPROM | on, 4 KiB: the platform window, addressed from its own zero; tfw-boot's span `[0x000, 0x140)` is refused to `EEPROM_WRITE` (the confirm request aside), the user span is `[0x140, 0x1000)`, and a span past the window is `RANGE`. RAM under QEMU: a write survives a `system_reset`, not a new QEMU process. The same on `mps2-an505` | `board_profile.h:24`, `:35`; `src/hal/cortex-m/hal_eeprom.c`; `src/caps/storage.c` |
| Tone | off by default, on in its own row: pin 1, LED1, toggled from the CMSDK timer TIMER0's interrupt; see Tone | `board_profile.h:27`–`34` |
| I2C, SPI, 1-Wire | off by default, each on in its own row: see Buses | `board_profile.h` |
| Pulse (`PULSE_IN`, `PULSE_CAPTURE`) | off by default, on in the `soft:PULSE=1` row, as on `mps2-an505`. The port's timer, `src/hal/cortex-m/hal_pulse.c`, polls the pin and counts SysTick's current value across reloads; the row links it on the Cortex-M7 and size-check measures it, and its QEMU wire row (`cortex-m-qemu-pulse-wire`, `TestQEMUPulseTimeout`) checks that `PULSE_IN` and `PULSE_CAPTURE` on a pin QEMU does not drive time out | `board_profile.h:36`–`42`; `profile.mk` `PULSE`, `BOARD_ROWS`; `mk/firmware.mk` `PULSE_SRCS` |

## Buses

I2C and SPI are on the shield 0 header, and QEMU v10.0.0's `hw/arm/mps2.c`
attaches no device to either; it leaves the GPIO block 1-Wire uses
unimplemented, so the line reads low. The default image links none of them (`profile.mk:60`–`74`).

| Bus | Block and base | Row | Sources |
|---|---|---|---|
| I2C | the shield 0 SBCon at `0x40029000`; no select, so no pin | `I2C=1` (`soft:I2C=1`) | AN500 (Arm DAI 0500B) memory map; QEMU `hw/arm/mps2.c` (Shield0 SBCon), `hw/i2c/arm_sbcon_i2c.c`; `board_profile.h:80`–`85` |
| SPI | the shield 0 PL022 at `0x40026000`; select pin 2, MISC bit 8 `SHIELD0_SPI_nCS` | `SPI=1` (`soft:SPI=1`) | AN500 3.1 Table 3-1, 3.7, 3.13 Table 3-6; QEMU `hw/arm/mps2.c` (Shield0 PL022), `hw/ssi/pl022.c`; `board_profile.h:87`–`95` |
| 1-Wire | bit-banged on the CMSDK AHB GPIO block 0 at `0x40010000`, bit 0: low is output-low, high is input with the bus pull-up; pin 3. The AVR master's slot timings, each slot with interrupts masked | `ONEWIRE=1` (`soft:ONEWIRE=1`) | Arm DDI 0479 4.6 (AHB GPIO); QEMU `hw/arm/mps2.c` (unimplemented GPIO); `src/hal/cortex-m/mps2/gpio_line.h`, `hal_bus.c`; `board_profile.h`, `TFW_MPS2_GPIO0_BASE` |

## Tone

`TONE=1` (row `soft:TONE=1`) links `src/hal/cortex-m/mps2/hal_tone.c`: a
square wave on pin 1, LED1, flipped from the interrupt of the CMSDK APB timer
TIMER0 at `0x40000000`, interrupt 8 (AN500 memory and interrupt maps; QEMU
v10.0.0 `hw/arm/mps2.c`; the timer's registers are Arm DDI 0479C's APB timer,
QEMU `hw/timer/cmsdk-apb-timer.c`). The reload is half the period at the
25 MHz core clock, less one. One tone at a time: a second `TONE` retargets,
`NOTONE` stops the timer and its interrupt and leaves the pin low. A half
period under 500 cycles, above 25 kHz, is refused with `TFW_ERR_RANGE`.
`make cortex-m-qemu-tone-wire BOARD=mps2-an500` runs the row's answers on
QEMU, which does not measure the pin.

## Boot

The base tfw-boot row, the same as `mps2-an505`'s: soft tier and measurement, P-256
verified in software, no OTA, no store (`profile.mk:43`–`45`), the row
`mps2-an500:soft:soft:p256-soft:0:0` of `BOOT_SUPPORTED` in `mk/boot.mk`,
which `make bootloader-check` builds. The soft root
keeps the UDS in tfw-boot's region and hands the CDI over in SRAM
(`board_profile.h:40`–`41`). `BOOT_SUPPORTED` also carries the store rows
`mps2-an500:soft:soft:p256-soft:0:1` and `:0:1:ab`.

The sizes of both MPS2 boards' rows, measured by `make bootloader-check` at
48d85486 with arm-none-eabi-gcc 14.2.1, in `mk/boot.mk` row order
(`mps2-an505` `:0:0`, `:1:0`, `:0:1`, `:0:1:ab`, then `mps2-an500` `:0:0`,
`:0:1`, `:0:1:ab`):

```
tfw-boot mps2-an505 flash   6956 / 32768    ram   324 / 32768 (soft tier, p256-soft)
tfw-boot mps2-an505 flash   8640 / 32768    ram   756 / 32768 (soft tier, p256-soft)
tfw-boot mps2-an505 flash   8216 / 32768    ram   580 / 32768 (soft tier, p256-soft)
tfw-boot mps2-an505 flash   9416 / 32768    ram  1100 / 32768 (soft tier, p256-soft)
tfw-boot mps2-an500 flash   7024 / 32768    ram   324 / 4194304 (soft tier, p256-soft)
tfw-boot mps2-an500 flash   8284 / 32768    ram   580 / 4194304 (soft tier, p256-soft)
tfw-boot mps2-an500 flash   9460 / 32768    ram  1100 / 4194304 (soft tier, p256-soft)
```

## What is tested, and how

`make integration-test-cortex-m-mps2-an500` (`mk/arch/cortex-m-targets.mk`)
builds the application and tfw-boot, signs the application with
`test/integration/cortex-m/cm_image.c`, and runs three QEMUs with
`test/integration/cortex-m/qemu_test.sh`:

- the signed image answers CAPS (board `0x42`, tier 1), takes a soft reset
  and answers again;
- a tampered image stays silent for a bounded window with QEMU still up;
- an image signed with another key does the same.

`make integration-test-cortex-m` runs it after the `mps2-an505` run. CI runs
both in the image job, in the toolchain image; `firmware-matrix` builds the
board with the rest.

`make integration-test-cortex-m-wire-mps2-an500` runs the tfwctl wire suite
on the board, then each bus's row beside `mps2-an505`'s, with the base row's
tfw-boot:

- the SPI row (`TestQEMUSPIXfer`, `sdk/go/cmd/tfwctl/spi_qemu_test.go`):
  `SPI_XFER` on pin 2 reads back `0x00` for every byte, since the PL022 has
  nothing on its bus and the image never sets loopback; pin 0 is refused with
  `ERR_BAD_PIN`;
- the I2C row (`TestQEMUI2CXfer`, `sdk/go/cmd/tfwctl/i2c_qemu_test.go`):
  `I2C_XFER` at `0x50` returns `ERR_NACK`, as a write, a read, and at 400 kHz,
  since `hw/i2c/bitbang_i2c.c` finds no device for the address byte;
  `0x60` is refused with `ERR_PROTECTED_REGION`, `0x80` with `ERR_RANGE`.

The base row also runs `TestQEMUResetCause`
(`sdk/go/cmd/tfwctl/reset_qemu_test.go`) on a boot of its own: SYS_INFO
reports `POWER_ON` after QMP's reset and `SOFTWARE` after RESET mode 0, whose
`AIRCR.SYSRESETREQ` QEMU's NVIC (`hw/intc/armv7m_nvic.c`) turns into a system
reset that keeps the marker word in SSRAM2. `TestQEMUFaultPanic`
(`sdk/go/cmd/tfwctl/fault_qemu_test.go`), on another boot, points the PC at
`0xE0000000` through QEMU's gdbstub; the execute-never fetch escalates to
HardFault, whose handler resets with the panic marker, and SYS_INFO reports
`PANIC`. The fault record at `RAM_END - 127` (`0x203FFF80`) then holds that
PC. The A/B row's `TestQEMUABWatchdog` parks a trial image in the NMI's loop.
The CMSDK watchdog, wired to the NMI in `hw/arm/mps2.c`, then bites, SYS_INFO
reports `WATCHDOG`, and the trial is rolled back. See the reset cause in
`docs/cortex-m/README.md`.

## Not met yet

This is an emulated test target: nothing in the tree runs on an M7 in
silicon. The reference M7 is the i.MX 95's
([`../multi-arch/imx95-design.md`](../multi-arch/imx95-design.md)). The test here
exercises neither the M7's MPU nor its caches, which QEMU does not model.
