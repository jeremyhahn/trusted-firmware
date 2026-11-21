# NUCLEO-H563ZI

> **Status: INFORMATIVE** · SPEC rows: `stm32h5`, `cortex-m` · Scope:
> `boards/nucleo-h563zi/` and `src/hal/cortex-m/stm32h5/soc_console.h`,
> built and host-tested, not yet run on the board · Last verified: 2026-11-21
>
> SPEC row `stm32h5`'s second target (#14), beside the
> [STM32H573I-DK](stm32h573i-dk.md): ST's NUCLEO-H563ZI as data on the same
> `stm32h5` SoC. The board is built in CI and its tables are checked on the
> host; nothing has been flashed or measured on it. The normative contract
> is [`adding-a-board.md`](adding-a-board.md).

**Sources.** ST's user manual for the board, UM3115, and ST's CMSIS header
for the part, `stm32h563xx.h`, were not held when this page was written
(ruling 109). Every value below that only they state is **TO CONFIRM**, in
this table and in the code comments that carry it.

## The board

| Item | Value | Status |
|---|---|---|
| Board | NUCLEO-H563ZI, MB1404, Nucleo-144 | |
| Part | STM32H563ZIT6, Cortex-M33, no crypto accelerators | |
| Flash | 2 MiB, two 1 MiB banks of 8 KiB sectors at `0x08000000` | as the STM32H573IIK3Q's |
| SRAM | 640 KiB; the port uses SRAM1, 256 KiB at `0x20000000` | as the STM32H573IIK3Q's |
| Debug and console | on-board ST-LINK/V3E, its Virtual COM port on USART3 | TO CONFIRM (UM3115) |
| USART3 pins | PD8 TX, PD9 RX, alternate function 7 | TO CONFIRM (UM3115) |
| USART3 base | `0x40004800`, APB1PERIPH_BASE_NS + `0x4800` | TO CONFIRM (`stm32h563xx.h`) |
| USART3 clock enable | RCC_APB1LENR, RCC + `0x9C`, USART3EN bit 18 | TO CONFIRM (`stm32h563xx.h`) |
| USART3 interrupt | USART3_IRQn 60 | TO CONFIRM (`stm32h563xx.h`) |
| USART3 kernel clock | PCLK1 at reset (USART3SEL 0), 64 MHz from the HSI | TO CONFIRM (RM0481) |
| GPIOD | bank index 3, RCC_AHB2ENR bit 3 | TO CONFIRM (`stm32h563xx.h`) |
| LD1 green | PB0 | TO CONFIRM (UM3115) |
| LD2 yellow | PF4 | TO CONFIRM (UM3115) |
| LD3 red | PG4 | TO CONFIRM (UM3115) |
| User button B1 | PC13 | TO CONFIRM (UM3115); not in the pin table yet |

## The port

- **Board id `0x47`**, in the Cortex-M range
  ([`board-id-registry.md`](board-id-registry.md)), `BoardNUCLEOH563ZI` in
  the Go SDK.
- **The SoC is unchanged.** The part is the STM32H573IIK3Q's family, so the
  clock (the undivided 64 MHz HSI), the flash wait states, the watchdog and
  the GPIO driver are `src/hal/cortex-m/stm32h5/`'s. The memory map and the
  budgets in `profile.mk` are the STM32H573I-DK's.
- **The console is board data.** `soc_console.h` names the console USART,
  its clock enable, its interrupt and its pins as `TFW_H5_CON_*`; the
  board's `board_profile.h` sets `TFW_H5_CONSOLE_USART3`, and every other
  stm32h5 board keeps the STM32H573I-DK's USART1 on PA9/PA10, whose
  preprocessed `hal_uart.c` is unchanged. Both USARTs' kernel clocks run at
  64 MHz at reset, so the divisor arithmetic is shared.
- **The pins** are the three user LEDs on ports GPIOB, GPIOF and GPIOG; a
  Nucleo-144's LEDs light at a high pin.
- **Soft tier, application only.** The part has no crypto accelerators and
  the board no secure element, so `BOARD_TIERS` is `soft`. There is no
  tfw-boot row for this board (`mk/boot.mk`); the boot geometry in
  `profile.mk` is stated for the day one is added.
- **TrustZone-M split, built only (TZ=1, #590).** `profile.mk` sets
  `TZ_MAP` to `src/hal/cortex-m/tz/tz_map_nucleo_h563zi.h`, whose SAU table
  splits the part in five regions (`docs/plans/590.md`):

  | Region | Base | Size |
  |---|---|---|
  | Secure code | `0x0C000000` | `0x1F800` |
  | Non-secure callable | `0x0C01F800` | `0x800` |
  | Non-secure code | `0x08020000` | `0x40000` |
  | Secure RAM | `0x30000000` | `0x10000` |
  | Non-secure RAM | `0x20010000` | `0x10000` |

  `make tz-firmware BOARD=nucleo-h563zi` (`mk/arch/cortex-m-targets.mk`)
  builds the secure image, its veneer import library and the non-secure
  image, and size-checks them; the host test
  `test_tz_regions_nucleo_h563zi` (`test/unit/test_tz_regions.c`) checks the
  board's SAU table. Running the images needs TZEN=1 and the bank 1 secure
  watermark over the secure image's 128 KiB: option bytes the owner sets
  (CA 583). Nothing in this tree writes an option byte.

## What is not verified

- Every value marked TO CONFIRM above.
- Nothing has run on the board: no image has been flashed, and the console,
  the LEDs and the watchdog have not been seen to work.
- The TZ=1 images: built and size-checked, never flashed; they need option
  bytes this tree does not write.
