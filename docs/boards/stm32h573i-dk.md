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
| Instruction cache | ICACHE is off at reset and tfw-boot does not enable it, so a flash write leaves nothing stale to invalidate | ICACHE off at reset is **not confirmed** against RM0481 |

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

## Feature matrix: the hw-test rows

Run 2026-11-17 (#543 slice 3) on the board with the #535 bench image (branch
`row/535-stm32h573i-dk-hw`, its `I2C=1` build, as #535 left the board). The
image is identified by a read-back, not a write: bank 1 (`0x08000000`, 1 MiB)
read with pyOCD `savemem` has SHA-256
`86b04107ed0d1f27301bc6ceb6f613671d9f90523e7244ca63208a40a659ef21`; tfw-boot
and the application slot (the first 160 KiB) have
`d0405ac1d39e9a12b57c8f143e21e3b76c59d74737dc1a13ecaf0d2c73064346`. Nothing
was flashed, and no option byte was written. The command, one row then the whole
list: `make hw-test BOARD=stm32h573i-dk PORT=<vcom> HW_RESET='cmd:pyocd reset
-t stm32h573iitx -u <probe serial>' HW_TEST_RUN='^TestFirmware<Row>$'`, then
the same command without `HW_TEST_RUN`. The lane had no STM32CubeProgrammer, so pyOCD
replaced the default reset. The board's first byte comes about 16 s after the
reset starts (tfw-boot's software P-256 check), so the seam gives it 30 s
(`Target.Bound`, `sdk/go/internal/peer`). Evidence for each verified row is
that row passing in the whole-list run, 385 s, every row from a probe reset.

- `TestFirmwarePing`: verified 2026-11-17, whole-list run.
- `TestFirmwarePingDeliversTheBootEventWithNoBootWait`: verified 2026-11-17,
  whole-list run.
- `TestFirmwarePingAfterAMalformedBurst`: verified 2026-11-17, whole-list run.
- `TestFirmwareCaps`: verified 2026-11-17, run alone and in the whole list
  (board id 0x43, the board's CAPS features).
- `TestFirmwareSys`: verified 2026-11-17, whole-list run (reset cause unknown,
  as `peerBoards` expects).
- `TestFirmwarePin`: verified 2026-11-17, whole-list run.
- `TestFirmwareWrite`: verified 2026-11-17, whole-list run.
- `TestFirmwareTimer`: verified 2026-11-17, whole-list run.
- `TestFirmwareEEPROM`: verified 2026-11-17, whole-list run.
- `TestFirmwareAnalog`: verified 2026-11-17, whole-list run (the refusal on
  the LED pin, LD2).
- `TestFirmwarePWM`: verified 2026-11-17, whole-list run (D6, TIM5_CH1).
- `TestFirmwarePulseIn`: verified 2026-11-17, whole-list run (D5).
- `TestFirmwareTone`: verified 2026-11-17, whole-list run (D3, TIM3).
- `TestFirmwareRefusesSPIItsCapsLacks`: verified 2026-11-17, whole-list run.
- `TestFirmwareRefusesOneWireItsCapsLacks`: verified 2026-11-17, whole-list
  run.
- `TestFirmwareAttestRefusedUnprovisioned`: **fails** 2026-11-17: `attest`
  gets no answer within the suite's 5 s command timeout (`command 0x60 did not
  respond after 5.006s`). Either the soft-tier signature at 64 MHz outlasts
  5 s, or the bench image, which writes a platform block, is not unprovisioned
  as the row assumes. Issue to file: the lane could not, since `gh` had no
  credentials.
- `TestFirmwareRefusesI2CItsCapsLacks`: **not run on its image** 2026-11-17:
  the board carries the `I2C=1` build, which reports i2c, so the row fails by
  design (`this row needs a peer without CAPS feature i2c`). It awaits a run on
  the default image.
- `TestFirmwareI2C`, `TestFirmwareI2CConfig` (the `I2C=1` image's rows):
  verified 2026-11-17, `HW_TEST_RUN='^TestFirmware(I2C|I2CConfig)$'`.

## What is not verified

Apart from the hw-test rows above, nothing here has run on the board. The port's register values are ST's source
code, not RM0481; the first bench slice reads RM0481 and DS14053 and checks
the clock, the console, the watchdog and tfw-boot's flash writes on the board. The rest of the
ST-sourced values above are from indexed summaries of UM3143 and UM3254, not
from the documents.
