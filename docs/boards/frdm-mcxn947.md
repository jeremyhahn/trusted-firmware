# FRDM-MCXN947

> **Status: INFORMATIVE** · SPEC row: `mcxn947` (in progress) · Scope:
> `boards/frdm-mcxn947/`, `src/hal/cortex-m/mcxn94x/`, `boot/hal/cortex-m/mcxn94x/`
> · Last verified: 2026-10-21
>
> The board as data and the SoC half of the generic Cortex-M port
> ([`../cortex-m/README.md`](../cortex-m/README.md)). The application and
> tfw-boot build; no board has run either, and nothing writes the part over a
> debug probe. The normative contract is [`adding-a-board.md`](adding-a-board.md).

**NXP's manuals were not read**: nxp.com answered 404 to every request from
the lane, its home page included. Every value below comes from NXP's
MCUXpresso SDK, pinned by commit and SHA-256 in
[`../reference/README.md`](../reference/README.md), "Board sources". A value
that no pinned file states is written **not confirmed**.

## The board

| | | Source |
|---|---|---|
| Board id | `0x46`, allocated in [`board-id-registry.md`](board-id-registry.md) | |
| MCU | MCXN947, dual Cortex-M33; this port runs core 0 only | SDK device header `MCXN947_cm33_core0.h` |
| Flash | 2 MiB at `0x10000000` (Secure alias) | SDK `MCXN947_cm33_core0_flash.ld` |
| RAM | 312 KiB at `0x30000000` with RAM ECC off (`m_data`, `0x4E000`) | SDK flash linker script; `system_MCXN947_cm33_core0.c` |
| Core clock | FRO12M, 12 MHz, AHB divider 1 | SDK `clock_config.c`, `BOARD_BootClockFRO12M` |
| Debug UART | LPUART4 on LP_FLEXCOMM4, FRO12M, 115200 baud, interrupt 39 | SDK `board.h` (`BOARD_DEBUG_UART_*`); device header |
| Debug UART pins | P1_8 = FC4_P0, P1_9 = FC4_P1, both pin function 2 (`kPORT_MuxAlt2`) with the input buffer on; PORT1 clocked first. Which of P0/P1 is RX in LPUART mode is **not confirmed** | SDK `hello_world` `pin_mux.c` lines 46, 49, 68, 105-107, 113-114, 128-130, 136-137 |
| LEDs | red P0_10, green P0_27, blue P1_2 | SDK `board.h` (`BOARD_LED_*`) |
| Buttons | SW2 P0_23, SW3 P0_6 | SDK `board.h` (`BOARD_SW*`) |

## What the port does not do

- Drive the EdgeLock secure subsystem, program a fuse, or start core 1.
- Disable the voltage and clock glitch detectors, which the SDK's
  `SystemInit` does.
- Write the part: pyOCD (`CM_FLASH_TOOL := pyocd`, target `mcxn947vdf`, from
  `pyocd pack install mcxn947vdf`) over the on-board MCU-Link. First
  `make board-record BOARD=frdm-mcxn947 CM_PROBE_SERIAL=<serial>` reads the
  whole flash at `0x10000000`; `make flash` and `make boot-flash` refuse until
  that record exists, and
  `make board-restore ... BOARD_RESTORE_IMAGE=build/records/<record>.flash.bin`
  writes it back and compares. No CMPA/CFPA read yet, and never a write there:
  their place in the map is not cited. Steps:
  [`../cortex-m/README.md`](../cortex-m/README.md).

## tfw-boot

The boot row is `frdm-mcxn947:soft:soft:p256-soft:0:0` in `mk/boot.mk`
`BOOT_SUPPORTED`: the software DICE root, the soft store, P-256 signature
verify, the 32 KB boot region every Cortex-M row uses. Whether the image fits
its region is measured by `make bootloader-check`, which needs the ARM
toolchain; the lane that added the row had none, so the fit is **not yet
measured**.

**UART.** `boot/hal/cortex-m/mcxn94x/boot_uart.c` drives LPUART4 on the
application's pins at the same 12 MHz FRO, polled, no interrupt enabled. The
boot does not run `hal_soc.c`'s early setup, so it clocks PORT1 and
LP_FLEXCOMM4 itself. The rate is `src/hal/cortex-m/mcxn94x/lpuart_baud.c`'s,
shared with the application. A rate the clock cannot divide to leaves the UART
off. `mcxn_uart.c` holds the register sequence and its test runs against a
register model (`boot/hal/cortex-m/mcxn94x/test/test_mcxn_uart.c`).

**Flash.** `boot_flash.c` writes through the boot ROM's flash driver, with
interrupts masked for the call:

| | Value | Source (mcux-sdk `8a289764`) |
|---|---|---|
| ROM API tree | `0x1303FC00`; the flash driver is its fifth word | `drivers/mcx_romapi/flash/src/fsl_flash.c` lines 19, 128-132 |
| Driver table | version, then `flash_init`, `flash_erase`, `flash_program` | `fsl_flash.c` lines 47-48, 51-57; calls at 155, 178, 190 |
| Config | `flash_config_t`: block base, total size, block count, page and sector size, 5-word FFR and 5-word mode config, `nbootCtx`, `useAhbRead` | `drivers/mcx_romapi/flash/fsl_flash.h` lines 253-308 |
| Erase key | `kFLASH_ApiEraseKey` = `'l','f','e','k'` = `0x6B65666C` | `fsl_flash.h` lines 148, 160 |
| Success | `kStatus_FLASH_Success` = 0 | `fsl_flash.h` line 79 |
| Program unit | 512 bytes | `fsl_flash.h` line 198 |
| Sector, page, size | 8192, 128, 2097152 bytes | `devices/MCXN947/MCXN947_cm33_core0_features.h` lines 947, 949, 951 |

`mcxn_flash.c` reads each touched 8 KiB sector into one static buffer, merges
the new bytes, and erases and programs the whole sector, which is both
sector- and 512-byte-aligned. A sector already holding the bytes is left
alone. A span past the flash, an unaligned sector or a driver reporting
another sector size is refused. A nonzero ROM status comes back as
`TFW_ERR_INTERNAL`. The buffer is zeroed after every write. The driver's
config lives on the stack for the call; the buffer is the only static the
writer owns. Its test runs against a ROM model
(`boot/hal/cortex-m/mcxn94x/test/test_mcxn_flash.c`).

## Not confirmed

The SDK, pinned in [`../reference/README.md`](../reference/README.md), stands
in for the manuals (ruling 109). What it settles is cited; what it cannot is
still to be read in the *MCXNx4x Reference Manual* or UM12018 before a board
runs an image:

1. **Pins: settled in part.** Function 2 on P1_8 (FC4_P0) and P1_9 (FC4_P1)
   is `pin_mux.c`'s. That FC4_P0 is RX and FC4_P1 TX in LPUART mode is not in
   a pinned file: reference manual, LP_FLEXCOMM signal table.
2. **Clock steps: open.** `BOARD_BootClockFRO12M` sets the core LDO and DCDC to
   1.0 V, flash read wait states to 0 and SRAM to 1.0 V (`clock_config.c`
   lines 97-116). `hal_soc.c` leaves those steps out, relying on the reset
   values matching; no pinned file states the reset values: reference manual,
   SPC and FMU chapters.
3. **WWDT divide-by-4: open.** The SDK clocks WDT0 at the 1 MHz FRO divided
   by `WDT0CLKDIV` + 1 (`fsl_clock.c` lines 1719-1753). The WWDT's own fixed
   prescaler is not in a pinned file: reference manual, WWDT chapter.
4. **Reset marker under RAM ECC: open.** Silicon behaviour no SDK file states:
   reference manual, SYSCON RAM ECC.
5. **ROM entry at `0x10000000`: open.** The SDK links the image there
   (`MCXN947_cm33_core0_flash.ld`); that the ROM enters a plain image there with
   the SAU off is the ROM's behaviour: reference manual, boot ROM chapter.
6. **Platform block: settled in part.** The sector is 8 KiB
   (`MCXN947_cm33_core0_features.h` line 949), so the block at `0x101FE000`
   is the last sector, alone. That erased flash reads back over the AHB
   without an ECC fault is not in a pinned file, and tfw-boot's
   read-modify-write depends on it as much as the application does: reference
   manual, FMU chapter.
