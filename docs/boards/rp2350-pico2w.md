# Raspberry Pi Pico 2 W (RP2350)

> **Status: INFORMATIVE** · SPEC rows: `rp2350`, `cortex-m` (future) · Scope:
> the board's first port slice, `boards/rp2350-pico2w/` · Last verified: 2026-10-21
>
> A profile of a board on the bench. The port's application is built in CI and
> has never run on the board: nothing here has been measured. Each
> value is cited to the vendor's document. A value that could not be read from
> one is written **not confirmed**. The normative contract a port will follow is
> [`adding-a-board.md`](adding-a-board.md); the decisions it will follow are
> ADR #14 ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md))
> and ADR #15 ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)),
> both PROPOSED.

Documents cited:

- **[PB]** *RP2350* product brief, Raspberry Pi Ltd, RP-008374-DS-1, published
  August 2024.
- **[DOC]** Raspberry Pi documentation, *Pico-series microcontrollers*, section
  "Raspberry Pi Pico 2 W" (web page, retrieved 2026-10-16). It links the board
  datasheet below.
- **[DS]** *Raspberry Pi Pico 2 W Datasheet: An RP2350-based microcontroller
  board*, RP-008304-DS, release 2 (03 July 2026, build d4e0f1799616), read in
  full by slice 3 and copied as
  [`../reference/pico-2-w-datasheet.pdf`](../reference/pico-2-w-datasheet.pdf).
- **[RP-DS]** *RP2350 Datasheet*, RP-008373-DS, release 8 (build
  80d627281ed5), read by slice 3 and copied as
  [`../reference/rp2350-datasheet.pdf`](../reference/rp2350-datasheet.pdf).

## The board

| | | Source |
|---|---|---|
| Board id | `0x44`, allocated in [`board-id-registry.md`](board-id-registry.md) (ADR open question 8) | `boards/rp2350-pico2w/board_profile.h` |
| MCU | RP2350; the board's package variant is **not confirmed** | [DOC] |
| Core | dual Arm Cortex-M33 with FPU, or dual Hazard3 RISC-V, selectable, at 150 MHz | [PB], "Key features"; [DOC] |
| Flash | **none on chip** for the RP2350A and RP2350B (the RP2354 variants carry 2 MB stacked); the board carries **4 MB** of external QSPI flash | [PB], package table; [DOC] |
| RAM | 520 KB on-chip SRAM, in ten banks | [PB], "Key features"; [DOC] |
| Debug probe | **none on the board**: a three-pin SWD connector (SWCLK, GND, SWDIO, no power pin), as through-hole pads or, on the "with headers" variant, a keyed three-pin JST-style connector. An external probe is needed | [DOC]; [DS] §3.9 "Debugging" (heading only read) |
| Console to the host | **not confirmed**. The board has a Micro-USB port to the RP2350's USB 1.1 controller ([DOC]), and the RP2350 has two UARTs ([PB]); no USB-to-UART bridge is described. The port's byte link is UART0 on GPIO0 (TX) and GPIO1 (RX), header pins 1 and 2; the GPIO numbers are **not confirmed** against [DS] | [PB]; [DOC] |
| Ethernet | **none**. Wireless only: Infineon CYW43439, 2.4 GHz 802.11n Wi-Fi and Bluetooth 5.2, on SPI at up to 33 MHz | [DOC]; [DS] §3.8 "Wireless interface" (heading only read) |

## Security facilities

All from [PB], "Overview" and "Key features". The RP2350 datasheet
(RP-008373-DS), whose security chapter is the register-level source, was not
read; its section numbers are **not confirmed**.

| | |
|---|---|
| TrustZone-M | the security architecture is "built around Arm TrustZone for Cortex-M" |
| OTP | 8 KB of antifuse OTP for key storage |
| Secure boot | optional boot signing, enforced by the on-chip mask ROM, with the key fingerprint in OTP |
| Boot decryption | protected OTP storage for an optional boot decryption key |
| Isolation | global bus filtering on Arm or RISC-V security and privilege levels; peripherals, GPIOs and DMA channels individually assignable to security domains |
| Crypto | a hardware SHA-256 accelerator and a hardware TRNG. **No public-key accelerator is named**, so P-256 signing would be in software |
| Fault injection | fast glitch detectors and hardware mitigations for fault-injection attacks |
| Documentation | the vendor states the security features, including the secure boot ROM, are documented without restriction |

## Upstream TF-M

As ADR #15 states it ("Platforms upstream at v2.3.1"): the RP2350 is an
upstream TF-M platform at **v2.3.1**.

## What this page bears on

- **ADR #15 open question 4** (crypto tier values): the key store here is OTP
  plus TrustZone, with no hardware ECC, which is neither AVR's element tier nor
  an SoC enclave.
- **ADR #15 open question 5** (the alias key): with software P-256 the M33 can
  derive a per-boot alias key from a CDI, which the ADR's context assumes.
- **The `rp2350` row's enrolment**: there is no Ethernet on this board, so a
  port enrols over Wi-Fi, USB or a UART, or adds a W5x00 module; which is not
  decided here.

## The port

The first slice (row `rp2350`): the board as data on the generic Cortex-M port
(#161), the application only. Its values were first written from memory of
the RP2350 datasheet; slice 3 checked each against [RP-DS] release 8
("Values against the datasheets", below).

| | | Where |
|---|---|---|
| Core | core 0, Arm, Secure with the SAU off, as the boot ROM leaves an Arm image | `boards/rp2350-pico2w/profile.mk` |
| Flash map | XIP at `0x10000000`: tfw-boot's 32 KiB, the application slot at `0x10008000` (128 KiB of code), the platform block in the last 4 KiB sector `0x103FF000` | `profile.mk`, `board_profile.h` |
| RAM | SRAM0-7, `0x20000000`-`0x2007FFFF`; SRAM8-9 left unused | `profile.mk` |
| Boot ROM | a minimal IMAGE_DEF block (secure Arm executable, no signature, no hash) right after the vector table, inside the first 4 KiB | `src/hal/cortex-m/rp2350/soc_calc.h`, `mk/arch/cortex-m/rp2350.ld` |
| Clocks | clk_ref, clk_sys and clk_peri from the 12 MHz crystal undivided; no PLL | `src/hal/cortex-m/rp2350/hal_soc.c` |
| Watchdog | 1 us ticks from clk_ref, 8 s period, resets everything but the oscillators | `hal_soc.c` |
| Pins | GPIO2-22 and GPIO26-28 through SIO; GPIO23-25 and 29 are the radio's; no LED (the board's is the CYW43439's) | `boards/rp2350-pico2w/board.c` |
| Not driven | the converter, PWM, I2C, SPI, USB, the radio | |

The HAL: `C` is the core directory `src/hal/cortex-m/`, `S` is
`src/hal/cortex-m/rp2350/`. Built in CI, never run.

| Function | Where | Behaviour on rp2350-pico2w |
|---|---|---|
| `tfw_hal_init` | C `hal_sys.c` | `TFW_OK`; S `hal_soc.c`'s early hook has already run clk_ref, clk_sys and clk_peri from the 12 MHz crystal |
| `tfw_hal_watchdog_arm`, `_pet` | C `hal_sys.c` → S `hal_soc.c` | the RP2350 watchdog on 1 us ticks; reset 8 s after the last pet |
| `tfw_hal_uart_init`, `_rx`, `_tx_try`, `_tx_idle` | S `hal_uart.c` | PL011 UART0 on GPIO0/1; receive interrupt-driven into a 64-byte ring on NVIC line 33; transmit into the 32-byte FIFO, `0` when it is full |
| `tfw_hal_gpio_mode`, `_write`, `_read` | S `hal_gpio.c` | SIO, GPIO0-29; input, output and input-pullup; the board's table lists only the header pins |
| `tfw_hal_adc_*` | S `hal_adc.c` | as on mps2-an505: no channel |

The second slice: tfw-boot, the `rp2350-pico2w:soft:soft:p256-soft:0:0` row
of `BOOT_SUPPORTED` (`mk/boot.mk`). Like the mps2-an505 row it hashes and
verifies P-256 in software, holds the soft DeviceID seed, and sheds OTA and the
external store. Slice 3 checked its values against [RP-DS] release 8, all but
the ROM flag's, which slice 4 took from pico-sdk (below). Its size, measured
by `make bootloader-check` at 48d85486 with arm-none-eabi-gcc 14.2.1:

```
tfw-boot rp2350-pico2w flash   7536 / 32768    ram  4420 / 524288 (soft tier, p256-soft)
```

| | | Where |
|---|---|---|
| Boot UART | UART0 on GPIO0/1, polled, the application's divisors; held in reset while off, so nothing of it outlives the handoff | `boot/hal/cortex-m/rp2350/boot_uart.c` |
| Flash writes | the 4 KiB sector read-modify-write that every write (platform block and image page) goes through; a sector that already holds the bytes is not erased; the sector buffer is zeroed after each write | `boot/hal/cortex-m/rp2350/rp_flash.c`, `boot_flash.c` |
| Boot ROM | `rom_table_lookup` through the halfword at `0x16`, flag `RT_FLAG_FUNC_ARM_SEC` (`0x0004`); codes `IF` `EX` `RE` `RP` `FC` `CX`; sector erase `0x20` over 4096 bytes. The sequence runs from SRAM (`.ramfunc`, copied with `.data`) with IRQs masked, and XIP comes back on the 03h read until the next reset. A sector offset that is not 4096-aligned or lies past the 4 MiB, and a ROM without any one of the six, are refused before XIP is left. The lookup itself is `rp_rom_lookup.c`, the one piece the host cannot run | `boot/hal/cortex-m/rp2350/rp_flash_rom.c`, `rp_rom_lookup.c`, `rp_flash.h`, `mk/arch/cortex-m/rp2350.ld` |
| Host test | `rp_flash.c` over a RAM flash; the ROM codes pinned as literals | `test/unit/boot/test_boot_rp2350_flash.c` |
| Host test | the flash map by literal under the board's profile: 4 MiB, tfw-boot, the application, the platform block in the last sector | `test/unit/boot/test_boot_rp2350_map.c` |
| Host test | the boot UART over a register model (`boot/hal/cortex-m/rp2350/test/cm_regs.h`): 115200 8N1 from 12 MHz (IBRD 6, FBRD 33, §12.1.7.1), UART0 out of reset through the SET/CLR aliases, GPIO0/1 function 2 and their pads, TX/RX through UARTDR, an unreachable baud left in reset, the pins returned at handoff; every address a literal from §12.1.8, §7.5.3, §9.11 | `boot/hal/cortex-m/rp2350/test/test_boot_rp2350_uart.c` |
| Host test | `rp_flash_rom.c` whole over a model of the ROM's function table and a 4 MiB flash: the six functions looked up by the codes as literals (`0x4649` `0x5845` `0x4552` `0x5052` `0x4346` `0x5843`); one sector erased (4096 bytes, block 4096, `0x20`) and programmed in 256-byte pages in the order `IF EX RE RP FC CX`, every ROM call with interrupts masked and erase and program only with XIP off; the last sector written; a misaligned offset, one past the flash and a ROM missing each function in turn refused with nothing called; XIP on and the interrupt mask restored after every path | `boot/hal/cortex-m/rp2350/test/test_boot_rp2350_rom.c` |

Still for the bench: the flash part's JEDEC id (below), the first real erase
and program through the ROM, and the console on GPIO0/1. No lane touches the
board.

Not yet built for the target in a lane: the lane image has no
`arm-none-eabi-gcc`. `make bootloader-check` in CI builds the row and fails if
it overflows the 32 KiB region.

The write path is `picotool` 2.0.0 or later over USB, the board held in
BOOTSEL (`CM_FLASH_TOOL := picotool`). `make board-record BOARD=rp2350-pico2w
CM_PROBE_SERIAL=<serial>` reads the whole 4 MiB from `0x10000000`
(`picotool save -r`) and dumps the OTP (`picotool otp dump`); `make flash` and
`make boot-flash` refuse until that record exists, then write with
`picotool load -v` and read back to compare; `make board-restore` with
`BOARD_RESTORE_IMAGE=build/records/<record>.flash.bin` writes the record back.
No OTP write, ever; it has not run on the board yet. Steps:
[`../cortex-m/README.md`](../cortex-m/README.md).

Not in either slice: any transport
for enrolment, and any OTP or secure-boot use. OTP and secure boot stay out of
every lane until the owner rules (the ADR's open question 1).

### The application's EEPROM

The RP2350 has no EEPROM; the application's `EEPROM_READ` and `EEPROM_WRITE`
keep the AVR's map (`TFW_EEPROM_SIZE`, `0x1140` bytes) over flash
(`src/hal/cortex-m/rp2350/hal_eeprom.c`):

| EEPROM address | Where | Write |
|---|---|---|
| `0x0000`-`0x013F` (`TFW_BOOT_EE_LEN`) | tfw-boot's span, read from the platform block at `0x103FF000` | refused (`TFW_ERR_RANGE`), except the 75-byte confirm request at `TFW_BOOT_CONFIRM_ADDR` |
| `0x0140`-`0x113F` | the user sector, `0x103FE000`-`0x103FEFFF` (`TFW_CM_USER_EE_BASE`), the 4 KiB directly below the platform block | a read-modify-write of the whole sector through tfw-boot's `rp_flash.c`, then read back; a mismatch is `TFW_ERR_INTERNAL` |

No EEPROM address reaches the platform sector: the user span is held inside
the user sector at compile time, and `test/unit/test_rp2350_eeprom.c` runs the
file over a NOR flash model and asserts the platform sector is never erased.

Power loss. Every write, even of one byte, erases and reprograms the whole
4 KiB user sector. Power lost between the erase and the end of the program
leaves the sector partly or wholly erased: every saved byte in
`0x0140`-`0x113F` can be lost, not only the bytes the command wrote. Keep
anything that must survive in more than one place, or rewrite it after a
power loss. The platform block is a different sector and is not touched.

The clock. The erase and program run with interrupts masked, tens of
milliseconds. The write reads TIMER0's microsecond count (§12.8.4.1, TIMERAWL)
before and after and credits the millisecond clock with what SysTick could not
count, carrying the part of a millisecond to the next write, so
`tfw_hal_millis` (scheduled GPIO holds, execution time) stays right across a
write. `tfw_cm_soc_early` starts TIMER0's 1 µs tick and takes it out of reset.

OTA_CONFIRM. The confirm request is stored as the generic Cortex-M file stores
it, into the XIP window, which on this part writes nothing to flash. Until a
slice routes it through the flash helper, OTA_CONFIRM's request does not reach
tfw-boot on this board, and a trial image is not confirmed by it.

Not validated on the board: no bench run has erased, written or read back the
user sector, or timed a write, on a Pico 2 W.

### Values against the datasheets

Slice 3 read both datasheets, copied in [`../reference/`](../reference/README.md):
the RP2350 datasheet (RP-008373-DS) **release 8**, build 80d627281ed5, and
the Pico 2 W datasheet (RP-008304-DS) **release 2**, build d4e0f1799616.
Section numbers are release 8's unless marked [DS]. Every value below was
checked; **none differed, so none was corrected**. The section is cited beside
each group in the source as well.

| Values | Section | Status |
|---|---|---|
| XIP `0x10000000`; SRAM0-7 `0x20000000`-`0x2007FFFF`, SRAM8-9 `0x20080000`-`0x20081FFF` | §2.2, tables 10-11 | matched |
| Atomic XOR `+0x1000`, SET `+0x2000`, CLR `+0x3000` | §2.1.3 | matched |
| RESETS `0x40020000`, RESET `+0x0`, DONE `+0x8`; IO_BANK0 bit 6, PADS_BANK0 9, UART0 26 | §2.2 table 13; §7.5.3 tables 566-567 | matched |
| XOSC `0x40048000`, CTRL/STATUS/STARTUP `+0x0`/`+0x4`/`+0xC`; FREQ_RANGE `0xAA0`, ENABLE `0xFAB` at 23:12, STABLE bit 31, DELAY 14 bits | §8.2.8 tables 630-634 | matched |
| XOSC startup base `(f * 1 ms) / 256` = 47 at 12 MHz | §8.2.4 | matched at 12 MHz; [RP-DS] rounds up, `tfw_rp_xosc_startup` to nearest |
| CLOCKS `0x40010000`, REF `0x30`-`0x38`, SYS `0x3C`-`0x44`, PERI `0x48`/`0x4C`; DIV.INT at bit 16; REF SRC xosc 2; SYS SRC clk_ref 0; PERI ENABLE bit 11, AUXSRC 0 clk_sys | §8.1.7 tables 588-596 | matched |
| TICKS `0x40108000`, WATCHDOG CTRL `+0x30` (ENABLE bit 0), CYCLES `+0x34` | §8.5.2 tables 662-663 | matched |
| WATCHDOG `0x400D8000`, CTRL ENABLE bit 30, PAUSE_DBG1/DBG0/JTAG `0x07000000`, LOAD 24 bits | §12.9.7 tables 1358-1360 | matched |
| PSM WDSEL `0x40018008`, bits 0-24 (`0x01FFFFFF`), ROSC bit 2 and XOSC bit 3 (`0xC`) | §7.4.4 tables 561, 564 | matched |
| Reset cause (`soc_reset.c`, from land/cmboards-fix): WATCHDOG.REASON `+0x008`, TIMER bit 0, FORCE bit 1; POWMAN `0x40100000`, CHIP_RESET `+0x02c`, HAD_POR 16, HAD_BOR 17, HAD_RUN_LOW 18, all HAD_ flags `0x1FEF0000` (bit 20 reserved); a PSM-only watchdog reset leaves CHIP_RESET as it was | §12.9.7 table 1361; §6.4 table 521; §6.3.9 (base); §7.3.3, §7.4 | matched; the datasheet names bit 28 `HAD_WATCHDOG_RESET_PSM`, pico-sdk `_RSM` |
| IO_BANK0 CTRL(n) `0x40028004 + 8n`, FUNCSEL 4:0, NULL `0x1F`, UART 2, SIO 5; GPIO0 F2 UART0 TX, GPIO1 F2 UART0 RX | §9.11.1 table 683; §9.4 table 677 | matched |
| PADS_BANK0 GPIO(n) `0x40038004 + 4n`: ISO 8, OD 7, IE 6, DRIVE 5:4 (4 mA = 1), PUE 3, PDE 2, SCHMITT 1 | §9.11.3 tables 883, 885 | matched |
| SIO `0xD0000000`: GPIO_IN `0x4`, OUT `0x10`, OUT_SET `0x18`, OUT_CLR `0x20`, OE_SET `0x38`, OE_CLR `0x40` | §2.2.5; §3.1.11 | matched |
| UART0 `0x40070000`, PL011 offsets; FR TXFE 7, TXFF 5, RXFE 4, BUSY 3; LCR_H `0x70`; CR `0x301`; IMSC `0x50`; ICR `0x7FF`; IRQ 33 | §12.1.8 tables 1081-1089; §3.2 table 95 | matched |
| The divisor arithmetic: 125 MHz at 115200 = IBRD 67, FBRD 52; divisors latch on the LCR_H write after them (both drivers write it last) | §12.1.7.1 | matched; now a literal in `test_rp2350_soc.c` |
| IMAGE_DEF: `0xFFFFDED3`, IMAGE_TYPE `0x42` size 1, flags EXE `0x1`, Secure `0x20`, Arm `0x000`, RP2350 `0x1000`; LAST `0x1FF`; link 0; `0xAB123579`; within the first 4 KiB | §5.9.1 table 487; §5.9.3.1 tables 492-493; §5.1.12 | matched |
| Boot ROM: lookup pointer halfword at `0x16`; code `c1 \| c2 << 8`; `IF` `EX` `RE` `RP` `FC` `CX` and their signatures; sector 4096, page 256, erase `20h` | §5.4.1 table 464; §5.4.8.3-.11 | matched |
| Pico 2 W pins: GPIO23 radio power, 24 SPI data/IRQ, 25 SPI CS, 29 SPI CLK/ADC3; GPIO26-28 ADC-capable | [DS] §2.1 | matched |

**Not in the datasheets.** Slice 4 looked each up in pico-sdk tag 2.1.1
(`bddd20f928ce`, listed in [`../reference/`](../reference/README.md)):

| Value | Source | Status |
|---|---|---|
| `RT_FLAG_FUNC_ARM_SEC` = `0x0004` | §5.4.1 names it, gives no number; pico-sdk `src/rp2_common/boot_bootrom_headers/include/boot/bootrom_constants.h` line 62 | sourced, matched |
| The XOSC startup's x64 multiplier | none: [RP-DS] names no multiplier; pico-sdk's default is 6 (`hardware_xosc/include/hardware/xosc.h` line 18), and neither `pico2.h` nor `pico2_w.h` sets it | unsourced, kept: it only waits longer (47 * 64 = 3008 fits the 14-bit field and the SDK's 8192 bound, about 64 ms once at reset) |
| "The boot ROM uses SRAM8-9" (`profile.mk`) | none: §2.2 gives the banks' addresses only, and pico-sdk's default map puts the core stacks there once an image runs (`pico_crt0/rp2350/memmap_default.ld` lines 28-29) | unsourced, kept: leaving 8 KiB unused costs nothing |
| The crystal's frequency | [DS] names the part (Abracon ABM8-272-T3), not its frequency; 12 MHz is §8.2.4's statement of the reference hardware | kept |

**The flash size: 4 MiB.** pico-sdk's board header for the Pico 2 W,
`src/boards/include/boards/pico2_w.h` line 79, sets `PICO_FLASH_SIZE_BYTES`
to `(4 * 1024 * 1024)`, the size every Pico 2 W image is built for, and [DS]
§1 says "4 MB of flash memory". The same page of [DS] names a Winbond
W25Q16JV, a 16 Mbit (2 MB) part, and §3.1 says "the on-board 2 MB QSPI
flash"; those two are taken as the datasheet's errors. `profile.mk`
(`FLASH_TOTAL`) and `board_profile.h` (`TFW_FLASH_TOTAL` `0x10400000`) say
4 MiB, and the platform block stays in the last 4 KiB sector, `0x103FF000`
to `0x103FFFFF`. `test/unit/boot/test_boot_rp2350_map.c` pins every region
by literal. On a 2 MB part `0x103FF000` would alias `0x101FF000`, outside
every region, so a wrong size cannot overwrite an image; the part's JEDEC id
is read on the bench before the first flash write.

## What is not verified

Nothing here has run on the board. The part's JEDEC id (the flash size above)
and the console wiring stay open until a lane with the board settles them.
