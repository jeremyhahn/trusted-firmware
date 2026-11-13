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
| Clocks | clk_ref, clk_sys and clk_peri from the 12 MHz crystal undivided; PLL_USB at 48 MHz for clk_adc alone, started by `tfw_hal_adc_init` (`hal_adc.c`) | `src/hal/cortex-m/rp2350/hal_soc.c` |
| Watchdog | 1 us ticks from clk_ref, 8 s period, resets everything but the oscillators | `hal_soc.c` |
| Pins | GPIO2-22 and GPIO26-28 through SIO; GPIO23-25 and 29 are the radio's; no LED (the board's is the CYW43439's) | `boards/rp2350-pico2w/board.c` |
| I2C | I2C0 on GPIO4 (SDA) and GPIO5 (SCL), pico-sdk 2.1.1's `PICO_DEFAULT_I2C`, with the pad pull-ups on; built only in the `soft:I2C=1` row (`I2C ?= 0`) | `profile.mk`, `board_profile.h` |
| PWM | eight header GPIOs, one per slice, each channel A: GP2, 4, 6, 8, 10, 12, 14 (slices 1-7) and GP16 (slice 0); 8-bit duty at 1 kHz | `board.c`, `board_profile.h` |
| Not driven | SPI, 1-Wire, USB, the radio | |

The HAL: `C` is the core directory `src/hal/cortex-m/`, `S` is
`src/hal/cortex-m/rp2350/`. Built in CI, never run.

| Function | Where | Behaviour on rp2350-pico2w |
|---|---|---|
| `tfw_hal_init` | C `hal_sys.c` | `TFW_OK`; S `hal_soc.c`'s early hook has already run clk_ref, clk_sys and clk_peri from the 12 MHz crystal |
| `tfw_hal_watchdog_arm`, `_pet` | C `hal_sys.c` → S `hal_soc.c` | the RP2350 watchdog on 1 us ticks; reset 8 s after the last pet |
| `tfw_hal_uart_init`, `_rx`, `_tx_try`, `_tx_idle` | S `hal_uart.c` | PL011 UART0 on GPIO0/1; receive interrupt-driven into a 64-byte ring on NVIC line 33; transmit into the 32-byte FIFO, `0` when it is full |
| `tfw_hal_gpio_mode`, `_write`, `_read` | S `hal_gpio.c` | SIO, GPIO0-29; input, output and input-pullup; the board's table lists only the header pins |
| `tfw_hal_tone_start`, `_stop` | S `hal_tone.c` | `TFW_FEATURE_TONE` 1, so CAPS lists tone: the tone pin is logical 5, GP7 (`TFW_TONE_PIN`), toggled through SIO's GPIO_OUT_XOR by TIMER1 alarm 0 on NVIC line 4; the alarm steps half a period on the 1 us tick, re-armed from the previous target so a late interrupt does not drift the period; 1 Hz to 25 kHz, 0 Hz and faster refused `TFW_ERR_RANGE`; a start while playing retargets with the old pin low; stop disarms and drives the pin low. Host test: `test/unit/test_rp2350_tone.c` over `test/model/cortex-m/rp2350/tone/` |
| `tfw_hal_adc_*` | S `hal_adc.c` | the SAR converter, one conversion per read on GPIO26-28 (ADC0-2, `adc_ch` 0-2; GPIO29/ADC3 is the radio's and the temperature sensor is not exposed); clk_adc at 48 MHz from PLL_USB, as §12.4.3 requires (REFDIV 1, FBDIV 100, POSTDIV 5/5; a PLL that never locks is `TFW_ERR_TIMEOUT`); the pad's IE cleared and OD set before a conversion; READY polled with a bound (`TFW_ERR_TIMEOUT`), CS.ERR is `TFW_ERR_INTERNAL`; 12-bit RESULT; only `TFW_ADC_REF_VCC` (ADC_AVDD). Host-tested against `test/model/cortex-m/rp2350/adc/adc_model.c` (`test_rp2350_adc.c`); built, not run on the board |
| `tfw_hal_i2c_init`, `_xfer` | S `hal_bus.c` | the DW_apb_i2c I2C0 master, polled, fast mode; SCL counts and spike length from clk_sys (12 MHz: HCNT 7, LCNT 17, SPKLEN 1, about 364 kHz for 400 kHz, never faster), a rate the 16-bit counts cannot make refused with `TFW_ERR_RANGE`. `TFW_ERR_NACK` from IC_TX_ABRT_SOURCE bits 0-4, `TFW_ERR_BUSY` on arbitration lost, `TFW_ERR_TIMEOUT` on a bus that does not come free; a STOP on every return that reached the bus (STOP_DET, else IC_ENABLE.ABORT). **Built and host-tested, not run on the board** |
| `tfw_hal_spi_*`, `tfw_hal_ow_*` | S `hal_bus.c` | `TFW_ERR_UNSUPPORTED` |
| `tfw_hal_pwm_write`, `_stop` | S `hal_pwm.c` | the PWM block, set up on first use, not at boot: PWM out of reset, then per slice the divider from `tfw_rp_pwm_div` (`soc_calc.h`; 12 MHz gives INT 46, FRAC 14), TOP 255 and EN, free-running; FUNCSEL 4 (PWM) on a pin's first write. Duty 0 is CC 0, 255 is CC 256 (TOP + 1), so both are exact. A row whose slice or channel is not the GPIO's is `TFW_ERR_BAD_PIN`. Stop clears the slice's EN, which stops both its channels, and hands the pin back to SIO driven low |

PWM is built, not run on the board. Its host test drives `hal_pwm.c` over a
flat register model (`test/model/cortex-m/rp2350/`) in
`test/unit/test_rp2350_pwm.c`: the divider maths, duty 0/128/255 compare
values, channels A and B in CC's two halves, stop, and a bad slice refused.
The register values (PWM base `0x400A8000`, slices `0x14` apart with CSR,
DIV, CTR, CC and TOP at `+0x0`-`+0x10`; CSR.EN bit 0; DIV INT bits 11:4 and
FRAC bits 3:0; CC A bits 15:0 and B bits 31:16; RESET bit 16; FUNCSEL 4) cite
[RP-DS] release 8 §12.5 and §9.4. The lane that wrote them had no PDF tooling,
so the table numbers have not yet been checked against the copied datasheet.
Check them before the first run on the board.

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

The I2C master (row `rp2350`) is host-tested over a register model of I2C0
(`test/unit/test_rp2350_i2c.c`, through the same stand-in `cm_regs.h`): a
write, a read, a write then a read with a RESTART, an address NACK on write
and on read, a data NACK, an abort the controller does not end with a STOP, a
stuck bus and a controller that never goes idle timing out, and the next
transfer after a timeout; the log of S, Sr, address, data and P is checked
for each. The SCL counts are literals in `test/unit/test_rp2350_soc.c`. No
I2C device has been wired to the board.

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
| I2C0 `0x40090000`; IC_CON, IC_TAR, IC_DATA_CMD (CMD 8, STOP 9, RESTART 10), FS_SCL_HCNT/LCNT, RAW_INTR_STAT (TX_ABRT 6, STOP_DET 9), the CLR_ registers, ENABLE (ABORT 1), STATUS, TX_ABRT_SOURCE, ENABLE_STATUS, FS_SPKLEN; RESETS I2C0 bit 4; GPIO4/5 F3 I2C0 | §12.2.17 tables 1108-1146; §7.5.3 table 566; §9.4 table 677 | read for row `rp2350` |
| The SCL counts: LCNT > SPKLEN+7, HCNT > SPKLEN+5, low LCNT+1 and high HCNT+SPKLEN+7 cycles; fast mode 1300 ns low, 600 ns high; spikes 50 ns, SPKLEN at least 1 | §12.2.14.1, figure 94; §12.2.14.2.1; §12.2.11 | read for row `rp2350`; literals in `test_rp2350_soc.c` |
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

Nothing here has run on the board, the I2C bus included. The part's JEDEC id (the flash size above)
and the console wiring stay open until a lane with the board settles them.

**The converter (`hal_adc.c`).** Its registers were written from pico-sdk
2.1.1's `hardware_regs` names (`adc.h`, `resets.h`, `clocks.h`,
`addressmap.h`) and not yet matched against the copied [RP-DS] tables: the
lane that wrote them had no PDF reader (the clk_adc and PLL_USB rows were
since matched against text extracted from the PDF). Each carries `TODO(datasheet)` in
`soc_regs.h` until a lane checks it:

| Value | Where to check | Status |
|---|---|---|
| ADC base `0x400A0000`; CS `+0x0` (EN 0, START_ONCE 2, READY 8, ERR 9, AINSEL 15:12), RESULT `+0x4` (11:0), FCS `+0x8`, DIV `+0x10` | §2.2 table 13; §12.4 register list | TODO(datasheet) |
| RESETS ADC bit 0 | §7.5.3 table 566 | TODO(datasheet) |
| CLK_ADC_CTRL `+0x6C` (ENABLE 11, AUXSRC 7:5, clksrc_pll_usb 0), CLK_ADC_DIV `+0x70` (INT 19:16) | §8.1.7 tables 603-604 | matched against the PDF's text; `TODO(datasheet)` kept until a reader with the rendered tables confirms |
| RESETS PLL_USB bit 15 | §7.5.3 table 567 | matched against the PDF's text |
| PLL_USB `0x40058000`; CS `+0x0` (LOCK 31, REFDIV 5:0), PWR `+0x4` (PD 0, POSTDIVPD 3, VCOPD 5), FBDIV_INT `+0x8` (11:0), PRIM `+0xC` (POSTDIV1 18:16, POSTDIV2 14:12) | §2.2 table 13; §8.6.5 tables 668-672 | matched against the PDF's text |
| clk_adc at 48 MHz from PLL_USB: 12 MHz / REFDIV 1 * FBDIV 100 = VCO 1200 MHz, / POSTDIV1 5 / POSTDIV2 5 | §12.4.3 ("The ADC requires a 48 MHz clock ... must be set up correctly before enabling the ADC"); §8.6.4's sequence and its pico-sdk PLL USB line | followed; the PLL is for clk_adc alone, clk_sys stays on the crystal |
| The pad for a conversion: OD set, IE, pulls and ISO clear, FUNCSEL NULL | §12.4 asks IE off and OD on; the pulls and ISO are this port's choice | kept |
| CS.ERR answered as `TFW_ERR_INTERNAL` | the wire has no conversion-error status | kept |

No reading has been taken on the board.

The tone's 20 us floor on the alarm step (`TFW_RP_TONE_MIN_STEP`, tones up to
25 kHz) is this port's own margin for the re-arming interrupt, not a
datasheet value; the tone on GP7 is checked on the register model only.
