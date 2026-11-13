# STM32H573I-DK

> **Status: INFORMATIVE** · SPEC rows: `stm32h5`, `cortex-m` · Scope:
> `boards/stm32h573i-dk/`, `src/hal/cortex-m/stm32h5/` and
> `boot/hal/cortex-m/stm32h5/`, built, not yet run
> on the board · Last verified: 2026-10-20
>
> A profile of a board on the bench and of the port's first slice (#14). The
> board is built in CI, as `mps2-an505` is, but nothing has been flashed or
> measured on it. Each value is cited to the vendor's source. A value that
> could not be read from one is written **not confirmed**. The normative
> contract the port follows is [`adding-a-board.md`](adding-a-board.md); the
> decisions it follows are ADR #14
> ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md))
> and ADR #15 ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)).

**ST's PDFs could still not be retrieved on 2026-10-20**: st.com refused every
download. The port's values come instead from ST's own source code on GitHub,
read at one commit each:

| Short name | Repository and commit | File |
|---|---|---|
| CMSIS header | `STMicroelectronics/cmsis-device-h5` @ `884b8dc78e41cbfca008363342b17f4a9e8641f7` | `Include/stm32h573xx.h`, `Source/Templates/system_stm32h5xx.c` |
| BSP | `STMicroelectronics/stm32h573i-discovery-bsp` @ `92b219d6a315c0974c5583094671c048c5dd41ec` | `stm32h573i_discovery.h`, `stm32h573i_discovery.c` |
| HAL | `STMicroelectronics/stm32h5xx-hal-driver` @ `16fea2d290119d6349b8f9a3b8538781ba1e1bdc` | `Src/stm32h5xx_hal_rcc.c`, `Src/stm32h5xx_ll_utils.c`, `Inc/stm32h5xx_hal_flash.h`, `Inc/stm32h5xx_hal_conf_template.h` |

The repositories are on github.com. The reference manual RM0481 and the
datasheet DS14053 have not been read, so a register's behaviour that only they
state is **not confirmed** and is named below where the port leans on it.

## The board

| | | Source |
|---|---|---|
| Board id | `0x43`, recorded in [`board-id-registry.md`](board-id-registry.md) | `boards/stm32h573i-dk/board_profile.h` |
| MCU | STM32H573II | UM3143, *Discovery kit with STM32H573II MCU*, introduction; revision and section not confirmed |
| Core | Arm Cortex-M33 with TrustZone, 250 MHz | ST product page *STM32H573II* (a web page, no document number); not confirmed against the datasheet DS14053 |
| Flash | 2 Mbytes at `0x08000000` (Non-secure alias), two banks of 1 Mbyte, 8 Kbyte sectors | CMSIS header: `FLASH_BASE_NS`, `FLASH_SIZE_DEFAULT`, `FLASH_BANK_SIZE`, `FLASH_SECTOR_SIZE` |
| RAM | 640 Kbytes; the port uses SRAM1, 256 Kbytes at `0x20000000` | ST product page *STM32H573II*; CMSIS header: `SRAM1_BASE_NS`, `SRAM1_SIZE` |
| Debug probe | STLINK-V3EC on the board, with mass storage, a Virtual COM port and a debug port | UM3143, introduction (features) |
| Console | the ST-LINK Virtual COM port: **USART1, TX PA9 and RX PA10, alternate function 7** | BSP: `COM1_UART`, `COM1_TX_PIN`, `COM1_RX_PIN`, `COM1_TX_AF`, `COM1_RX_AF` |
| User LEDs | LD1 green PI9, LD2 orange PI8, LD3 red PF1, LD4 blue PF4; each lights at a **low** pin | BSP: `LED1_PIN` to `LED4_PIN`, `LED1_GPIO_PORT` to `LED4_GPIO_PORT`; the BSP's `BSP_LED_On` drives the pin low |
| Ethernet | **on the board**: 10/100 Mbit/s, IEEE 802.3-2002, from the MCU's Ethernet MAC | UM3143, introduction (features) |
| Ethernet PHY part and connector | **not confirmed** | |
| Other radios | a Wi-Fi module | UM3143, introduction |

## The port

What `boards/stm32h573i-dk/` and `src/hal/cortex-m/stm32h5/` set, and why.

| | | Source |
|---|---|---|
| Security state | TrustZone **off**, the part's factory state, so every address is the Non-secure alias and the core runs from `0x08000000`. Whether the port should run Secure with TrustZone on is the row's open decision (ADR #15) | CMSIS header (`*_BASE_NS`); TZEN as the factory default is **not confirmed** against RM0481 |
| Memory map | tfw-boot 32 KiB at `0x08000000`; the application slot 128 KiB at `0x08008000`; the platform block in bank 1's last sector, 8 KiB at `0x080FE000`; `.data`, `.bss`, the stack, the reset marker and the handoff block in SRAM1, the handoff block at its top | `boards/stm32h573i-dk/profile.mk`, `board_profile.h`; sector size from the CMSIS header |
| Clock | the HSI, 64 MHz, with its divider HSIDIV forced to 1 and every bus prescaler at 1, so `TFW_CM_CPU_HZ` is 64 MHz. Three flash wait states, the VOS3 table's for 64 MHz, written before the divider | HAL: `stm32h5xx_hal_rcc.c` (the reset clock), `stm32h5xx_ll_utils.c` (the VOS3 latency table); CMSIS `system_stm32h5xx.c` (`SystemInit` clears HSIDIV) |
| Console divisor | USART1's kernel clock at reset is PCLK2, so BRR is 64 MHz over the rate: 556 at 115200 baud | HAL: `stm32h5xx_hal_rcc.c`; `tfw_h5_usart_brr` in `src/hal/cortex-m/stm32h5/h5_calc.h` |
| Console interrupt | USART1 is interrupt 58, so the port's vector table holds 64 external interrupts | CMSIS header: `USART1_IRQn` |
| Watchdog | the IWDG on the LSI, 32 kHz typical, a 4 s period (prescaler /32, reload 3999). It cannot be stopped once started, so an image's reset handler pets it rather than disarming it | HAL: `stm32h5xx_hal_conf_template.h` (`LSI_VALUE`); the LSI's spread and the IWDG's state across a system reset are **not confirmed** against RM0481 |
| Writing the board | STM32CubeProgrammer CLI (`STM32_Programmer_CLI`, ST UM2237) over the STLINK-V3EC. `make board-record BOARD=stm32h573i-dk CM_PROBE_SERIAL=<serial>` reads the whole 2 MB flash at `0x08000000` and the option bytes (`-ob displ`); `make flash` and `make boot-flash` refuse until that record exists; `make board-restore` with `BOARD_RESTORE_IMAGE=build/records/<record>.flash.bin` writes it back and compares. No option-byte write, RDP or TrustZone change, ever. Not yet run on the board. Steps: [`../cortex-m/README.md`](../cortex-m/README.md) | `mk/arch/cortex-m.mk`, `boards/stm32h573i-dk/profile.mk` |
| tfw-boot's flash writes | **driven, not yet run on the board**: tfw-boot writes the slot and the platform block through the flash interface's Non-secure registers. It unlocks with `FLASH_KEY1` then `FLASH_KEY2` in NSKEYR. It programs a quad-word (16 bytes) at a time with PG. It erases one 8 KiB sector with SER, SNB and STRT, plus BKSEL for bank 2. It waits on BSY, WBNE and DBNE in NSSR, and clears NSSR's error flags 17 to 23 and EOP through NSCCR. A write that matches the flash is skipped. One whose quad-words are all erased is programmed only. Any other rewrites each 8 KiB sector it touches through a RAM copy. The application has no flash access (`boot/boot_hal.h`). The offsets, bits and keys are **not confirmed** against RM0481 | CMSIS header: `FLASH_TypeDef`, `FLASH_SR_*`, `FLASH_CR_*`, `FLASH_CCR_*`; HAL: `stm32h5xx_hal_flash.h` (`FLASH_KEY1`, `FLASH_KEY2`). The sequences follow the HAL's `FLASH_Program_QuadWord`, `FLASH_Erase_Sector` and `FLASH_WaitForLastOperation`, **not confirmed** at the pinned commit; `boot/hal/cortex-m/stm32h5/h5_flash.c`, tested by `test/unit/boot/test_boot_stm32h5_flash.c` |
| PWM | **built, not yet run on the board**: TIM5, a general-purpose timer nothing else in the port drives, on the Arduino header's D6 (PH10, TIM5_CH1) and D5 (PH11, TIM5_CH2), alternate function 2. Eight-bit duty at about 1 kHz, as the AVR and the ESP32 give: ARR 254, PSC 250 from 64 MHz (999.9 Hz), PWM mode 1 with preload, so duty 0 is never high and 255 always is. The timer is set up on the first write, not at boot; a channel's pin goes to the timer on that channel's first write, and stop disables the channel and drives the pin low as an output. TIM5's clock is APB1's, 64 MHz with its prescaler at 1 | CMSIS header: `TIM5_BASE_NS`, `TIM_TypeDef`, `TIM_CR1_*`, `TIM_EGR_UG`, `TIM_CCMR1_OC1M*`/`OC1PE`/`OC2M`/`OC2PE`, `TIM_CCER_CC1E`/`CC2E`, `RCC_APB1LENR_TIM5EN`; HAL: `stm32h5xx_ll_tim.h` (`LL_TIM_OCMODE_PWM1`), `stm32h5xx_hal_gpio_ex.h` (`GPIO_AF2_TIM5`); the pins: ST's board configuration for the kit (STM32_open_pin_data, `PH10 ARD_D6 S_TIM5_CH1`, `PH11 ARD_D5 S_TIM5_CH2`). The timer clock equal to PCLK1 at prescaler 1 is **not confirmed** against RM0481. `src/hal/cortex-m/stm32h5/hal_pwm.c`, `h5_calc.h`, host-tested by `test/unit/test_stm32h5_pwm.c` |
| Instruction cache | ICACHE is off at reset and tfw-boot does not enable it, so a flash write leaves nothing stale to invalidate | ICACHE off at reset is **not confirmed** against RM0481 |

## tfw-boot

One tfw-boot row, `stm32h573i-dk:soft:soft:p256-soft:0:0` of `BOOT_SUPPORTED`
in `mk/boot.mk`, which `make bootloader-check` builds, in the 32 KiB region at
the base of flash. Its size, measured by `make bootloader-check` at 48d85486
with arm-none-eabi-gcc 14.2.1:

```
tfw-boot stm32h573i-dk flash   8756 / 32768    ram  8516 / 262144 (soft tier, p256-soft)
```

## Security facilities

| | | Source |
|---|---|---|
| TrustZone-M | yes: Armv8-M with the Security Extension | ST product page *STM32H573II*; not confirmed against DS14053 |
| ST's secure world | **STM32Trust TEE Secure Manager** (STM32TRUSTEE-SM): a PSA-compliant secure world for Armv8-M, with secure boot, cryptography, internal trusted storage, initial attestation and firmware update | UM3254, *Secure Manager for STM32H573xx microcontrollers*, Rev 8, August 2025; section not confirmed. Feature list also on ST's STM32TRUSTEE-SM page |
| Secure Manager access kit | X-CUBE-SEC-M-H5, distributed under export-control conditions | ST's STM32TRUSTEE-SM page |
| Certification targets | PSA Certified Level 3 and SESIP 3 are the part's targets; the SESIP guidance is UM3238 | ST product page *STM32H573II*; UM3238, *STM32H573xx STM32TrusteeSM security guidance for SESIP 3 certification*, revision not confirmed |
| Factory device certificate | **not confirmed** (the `stm32h5` row states one; no ST document was read that says so) | |
| Hardware crypto (PKA, AES, HASH) | **not confirmed**; the port's first slice uses the soft tier only | |

## Upstream TF-M

As ADR #15 states it ("Platforms upstream at v2.3.1"): the STM32H573I-DK is an
upstream TF-M platform at **v2.3.1**; the NUCLEO-H563ZI is not.

## What this page bears on

- **ADR #15 open question 2** (vendor secure worlds): the Secure Manager is
  documented in UM3254, and its access kit is export-controlled. The licence
  check that question asks for starts there.
- **ADR #15 open question 1**: the upstream TF-M platform is this board, not
  the NUCLEO-H563ZI.
- **The `stm32h5` row's "enrolling over Ethernet"**: this board has Ethernet on
  board, so no W5x00 module is needed for it.

## Owner question

**The whole-sector rewrite.** A quad-word on this part is programmed once
between erases, and the smallest erase is an 8 KiB sector. So a write over
programmed bytes copies the sector to RAM, erases it and programs it back. A
reset inside that window loses the sector's other bytes. Two writes hit it:

- the platform block, one sector, rewritten whenever a field already written
  changes;
- an OTA page (256 bytes) over an older image, which rewrites its sector once
  per page: 32 erases of each sector per update.

The rewrite stands until the owner rules on it. The alternatives are a second
sector for the platform block written alternately, and an OTA writer that
erases a sector once and then programs it page by page.

## What is not verified

Nothing here has run on the board. The port's register values are ST's source
code, not RM0481; the first bench slice reads RM0481 and DS14053 and checks
the clock, the console, the watchdog, the PWM and tfw-boot's flash writes on
the board. The rest of the
ST-sourced values above are from indexed summaries of UM3143 and UM3254, not
from the documents.
