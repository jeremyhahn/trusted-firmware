# STM32H573I-DK

> **Status: INFORMATIVE** · SPEC rows: `stm32h5`, `cortex-m` · Scope:
> `boards/stm32h573i-dk/`, `src/hal/cortex-m/stm32h5/` and
> `boot/hal/cortex-m/stm32h5/`, run on the board
> 2026-11-17 (#535; see "Bench run") · Last verified: 2026-11-17
>
> A profile of a board on the bench and of the port's first slice (#14). The
> board is built in CI, as `mps2-an505` is; the rows run on the bench on
> 2026-11-17 are in "Bench run" and the "Feature matrix" below. Each value is cited to the vendor's source. A value that
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
| HAL | `STMicroelectronics/stm32h5xx-hal-driver` @ `16fea2d290119d6349b8f9a3b8538781ba1e1bdc` | `Src/stm32h5xx_hal_rcc.c`, `Src/stm32h5xx_ll_utils.c`, `Inc/stm32h5xx_hal_flash.h`, `Inc/stm32h5xx_hal_conf_template.h`, `Inc/stm32h5xx_ll_tim.h`, `Inc/stm32h5xx_hal_gpio_ex.h`; the ADC's `Src/stm32h5xx_hal_adc.c`, `Src/stm32h5xx_hal_adc_ex.c`, `Inc/stm32h5xx_ll_adc.h`, `Inc/stm32h5xx_ll_rcc.h` |
| Pin data | `STMicroelectronics/STM32_open_pin_data` @ `7d1f1514ed5583ec5007ad91236b4e1d377295b1` | `boards/H77_Discovery_STM32H573I-DK_STM32H573IIK3Q_Board_AllConfig.ioc` (the kit's pin labels), `mcu/IP/GPIO-STM32H56x_gpio_v1_0_Modes.xml` (each pin's alternate functions) |
| Connector | `zephyrproject-rtos/zephyr` @ `270ee49bd8fe412446d862a56aae16a0e245104d` | `boards/st/stm32h573i_dk/arduino_r3_connector.dtsi` |
| Pin table | `zephyrproject-rtos/hal_stm32` @ `9dd7ccaefb5df6bf750d4d52cfd4b88eed2d0923` | `dts/st/h5/stm32h573iikxq-pinctrl.dtsi` |

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
| Ethernet PHY | **verified on the board 2026-11-17**: a Microchip LAN8742A at MDIO address 0, PHYID1 `0x0007`, PHYID2 `0xC131` (OUI bits `0x1F0`, model `0x13`, revision 1), read by the `ETH_PROBE=1` image's boot line `eth: phy addr=0 id=0007:C131 link=up`; commit 889bddc7, the ETH_PROBE application's sha256 `62c2b4efe540ad0fc412c4eed4e69371af91d78ec849366f3fcfb5f1063582fa` (`trusted-firmware.hex`), written signed in the bench image (see "Bench run"), whose packed sha256 is `38aadc621a5250e13e26b37392da77325b0280d068c9582122e2d9a1b5bc1dac`. RMII (SBS PMCR ETH_SEL_PHY = RMII), the MAC's clocks on, MDC at HCLK/42, 1.52 MHz; the pins PA1 REF_CLK, PA2 MDIO, PA7 CRS_DV, PC1 MDC, PC4 RXD0, PC5 RXD1, PG11 TX_EN, PG12 TXD1, PG13 TXD0, alternate function 11. `src/hal/cortex-m/stm32h5/hal_eth_mdio.c`; the decode host-tested by `test/unit/test_h5_eth_mdio.c`. The connector is an RJ45 on the board, cabled to the dev network | Pin data, the kit's board file (`PA1.Signal=ETH_REF_CLK` and the eight others, `ETH.MediaInterface=HAL_ETH_RMII_MODE`); CMSIS header: `ETH_BASE_NS`, `ETH_TypeDef` (`MACMDIOAR`, `MACMDIODR`), `ETH_MACMDIOAR_*`, `RCC_AHB1ENR_ETHEN`/`ETHTXEN`/`ETHRXEN`, `SBS_PMCR_ETH_SEL_PHY`; offsets **not confirmed** against RM0481 |
| Ethernet link | **up, verified on the board 2026-11-17**: BMSR bit 2 set on the second read (the bit latches low), the same boot line | as above |
| Ethernet frames | **verified on the board 2026-11-17**: the MAC and DMA in poll mode (`src/hal/cortex-m/stm32h5/hal_eth_mac.c`, four TX and four RX descriptors, 1536-byte buffers, the speed and duplex from the LAN8742A's PSCSR), host-tested by `test/unit/test_h5_eth_mac.c`; on the board the frames of a whole DHCP exchange went out and came in. Commit b0226d8f, the `ETH_MAC=1` application's sha256 `c3de81f3a65ba7260803ec2f3509b0dfcb7ec217440f3b24c8b4589c010d2f31` (`trusted-firmware.bin`), written signed in the bench image (see "Bench run"), whose packed sha256 is `26ba05ce4d03b41bb550cd5fa8b97b6ddcce0d9789b3f2c8386a97f600e1fa5d` (`.bin`) | RM0481 (the ETH MAC, MTL and DMA registers, the descriptor formats); LAN8742A data sheet (PSCSR) |
| DHCP lease | **verified on the board 2026-11-17**: the same image printed `eth: dhcp bound` and the leased address on the console about 15.6 s after a debugger reset, 10 s of it tfw-boot's P-256 check; the address was on the dev network (VLAN 101) | as above |
| IP stack | **lwIP 2.2.1** (`STABLE-2_2_1_RELEASE`), NO_SYS raw API polled from the main loop, IPv4 with ARP, ICMP, UDP, TCP and DHCP, vendored unmodified under `third_party/lwip` (`VERSION` pins the commit and the tree hash); the options are `src/hal/cortex-m/stm32h5/lwipopts.h`; built only with `ETH_MAC=1` | [`../multi-arch/adr-stm32h5-ethernet.md`](../multi-arch/adr-stm32h5-ethernet.md) |
| tfw-wire over TCP | **to re-run after fix-584k (outbound transport)**. Since #584's fix-584k the `ETH_MAC=1` image opens one outbound TCP connection to the endpoint (peer and port) in its network block and listens on nothing (lwIP raw API, `hal_eth_wire_tcp.c`); a refused or dropped connection is redialled after 1 s, the wait doubling per refusal up to 16 s, the block read again each time, and the netif reconciled with it before the dial; `make hw-test PORT=tcp-listen:<host-port>` accepts that connection (below). Its address, mask, gateway and MAC come from the network block (`src/net/netcfg.c`); DHCP runs only when the block's DHCP flag is set; the UID-derived, locally administered MAC is used only while the block's MAC is all zero; with no valid block the device stays off the network and serves the UART. Host-tested by `test/unit/test_h5_wire_connect.c` and `test/unit/test_h5_netif.c`. The 2026-11-17 verification (#582) was of the earlier inbound listener on port 21574, one connection at a time, a new one replacing the held one; `hal_eth_wire.c` serves it and the UART to the one transport, replies on the wire the request came on, events on the UART. Commit 1cf43fdf; the P4's row set passes over `PORT=tcp:<board>:21574` but for the boot-event row (see the feature matrix) | `src/hal/cortex-m/stm32h5/hal_eth_wire.h` |
| Other radios | a Wi-Fi module | UM3143, introduction |

## The port

What `boards/stm32h573i-dk/` and `src/hal/cortex-m/stm32h5/` set, and why.

| | | Source |
|---|---|---|
| Security state | TrustZone **off**, the part's factory state, so every address is the Non-secure alias and the core runs from `0x08000000`. Whether the port should run Secure with TrustZone on is the row's open decision (ADR #15) | CMSIS header (`*_BASE_NS`); TZEN as the factory default is **not confirmed** against RM0481 |
| Memory map | tfw-boot 32 KiB at `0x08000000`; the application slot 128 KiB at `0x08008000`; the platform block in bank 1's last sector, 8 KiB at `0x080FE000`; the application's EEPROM in bank 2's last sector, 8 KiB at `0x081FE000`; `.data`, `.bss`, the stack, the reset marker and the handoff block in SRAM1, the handoff block at its top | `boards/stm32h573i-dk/profile.mk`, `board_profile.h`; sector size from the CMSIS header |
| Clock | the HSI, 64 MHz, with its divider HSIDIV forced to 1 and every bus prescaler at 1, so `TFW_CM_CPU_HZ` is 64 MHz. Three flash wait states, the VOS3 table's for 64 MHz, written before the divider | HAL: `stm32h5xx_hal_rcc.c` (the reset clock), `stm32h5xx_ll_utils.c` (the VOS3 latency table); CMSIS `system_stm32h5xx.c` (`SystemInit` clears HSIDIV) |
| Console divisor | USART1's kernel clock at reset is PCLK2, so BRR is 64 MHz over the rate: 556 at 115200 baud | HAL: `stm32h5xx_hal_rcc.c`; `tfw_h5_usart_brr` in `src/hal/cortex-m/stm32h5/h5_calc.h` |
| Console interrupt | USART1 is interrupt 58, so the port's vector table holds 64 external interrupts | CMSIS header: `USART1_IRQn` |
| Watchdog | the IWDG on the LSI, 32 kHz typical, a 4 s period (prescaler /32, reload 3999). It cannot be stopped once started, so an image's reset handler pets it rather than disarming it | HAL: `stm32h5xx_hal_conf_template.h` (`LSI_VALUE`); the LSI's spread and the IWDG's state across a system reset are **not confirmed** against RM0481 |
| I2C | **verified on the board 2026-11-17**: `make firmware BOARD=stm32h573i-dk I2C=1`, commit c97a2728, image sha256 `1d89e98b03f1996af0d82cd7ed719226f49c5927e7adee1f42f7c567dc68d228`; CAPS lists `i2c`, a read at 0x50, 0x3C and 0x68 and a write at 0x50 answer NACK on the bare bus, `i2c-config 400000` is taken and the link answers after. No element was on the bus, so no ACK path ran: I2C4, SCL PB8 and SDA PB9, alternate function 6, open drain with the pull-up on, the bus ST's BSP drives. That these pins reach the Arduino header's SCL/SDA is **not confirmed** against UM3143. A polled master (`src/hal/cortex-m/stm32h5/hal_bus.c`): START, address, data, a repeated START into a read, and a software STOP on every return that reached the bus; a NACK on the address or a byte is `TFW_ERR_NACK`, a bus that stays busy or a flag that never comes is `TFW_ERR_TIMEOUT`. The kernel clock is PCLK3, 64 MHz; TIMINGR is `0x60702729` at 100 kHz and `0x10A11626` at 400 kHz, the analog filter on. Off by default (`I2C ?= 0`); the `soft:I2C=1` row builds it. SPI and 1-Wire refuse with `TFW_ERR_UNSUPPORTED`. Tested against a register model, `test/unit/test_stm32h5_i2c.c`; no element has been on the bus | BSP: `stm32h573i_discovery_bus.h` (`BUS_I2C4`, `BUS_I2C4_SCL_PIN`, `BUS_I2C4_SDA_PIN`, `BUS_I2C4_SCL_AF`, `BUS_I2C4_SDA_AF`), `stm32h573i_discovery_bus.c` (`I2C4_MspInit`, `I2C_GetTiming` and its `I2C_Charac` table, which gives both TIMINGR values for 64 MHz); CMSIS header: `I2C4_BASE_NS`, `I2C_TypeDef`, `I2C_CR1_*`, `I2C_CR2_*`, `I2C_ISR_*`, `I2C_ICR_*`, `RCC_APB3ENR_I2C4EN`, `GPIO_TypeDef`; HAL: `stm32h5xx_hal_gpio_ex.h` (`GPIO_AF6_I2C4`), `stm32h5xx_ll_rcc.h` (`LL_RCC_I2C4_CLKSOURCE_PCLK3`), `stm32h5xx_hal_i2c.c` (`I2C_IsErrorOccurred`, the STOP after a NACK), `stm32h5xx_ll_i2c.h` |
| Writing the board | **verified on the board 2026-11-17** with pyOCD (`CM_FLASH_TOOL=pyocd CM_PROBE_TARGET=stm32h573iikxq`, pack Keil.STM32H5xx_DFP 2.3.1): `make board-record` read the whole 2 MB and its sha256 file checks; `make boot-flash` and `make flash` read back equal after commit c97a2728, which halts the core before pyOCD's read-back (a running core read back zeros). pyOCD's record holds no option bytes (its extra file is empty). STM32CubeProgrammer was not used. The STM32CubeProgrammer path: CLI (`STM32_Programmer_CLI`, ST UM2237) over the STLINK-V3EC. `make board-record BOARD=stm32h573i-dk CM_PROBE_SERIAL=<serial>` reads the whole 2 MB flash at `0x08000000` and the option bytes (`-ob displ`); `make flash` and `make boot-flash` refuse until that record exists; `make board-restore` with `BOARD_RESTORE_IMAGE=build/records/<record>.flash.bin` writes it back and compares. No option-byte write, RDP or TrustZone change, ever. Steps: [`../cortex-m/README.md`](../cortex-m/README.md) | `mk/arch/cortex-m.mk`, `boards/stm32h573i-dk/profile.mk` |
| tfw-boot's flash writes | **not runnable on the board 2026-11-17**: the board's one tfw-boot row has no OTA, so tfw-boot never writes the slot, and it writes the platform block only on a trial. The shared programming sequence (`h5_flash.c`) did run from the application, in the EEPROM row below: tfw-boot writes the slot and the platform block through the flash interface's Non-secure registers. It unlocks with `FLASH_KEY1` then `FLASH_KEY2` in NSKEYR. It programs a quad-word (16 bytes) at a time with PG. It erases one 8 KiB sector with SER, SNB and STRT, plus BKSEL for bank 2. It waits on BSY, WBNE and DBNE in NSSR, and clears NSSR's error flags 17 to 23 and EOP through NSCCR. A write that matches the flash is skipped. One whose quad-words are all erased is programmed only. Any other rewrites each 8 KiB sector it touches through a RAM copy, then reads the whole sector back against the copy. The four stores of each quad-word run with PRIMASK set, saved and put back, so tfw-boot stays masked and the application's EEPROM writes (below) are not split by SysTick or USART1. The offsets, bits and keys are **not confirmed** against RM0481 | CMSIS header: `FLASH_TypeDef`, `FLASH_SR_*`, `FLASH_CR_*`, `FLASH_CCR_*`; HAL: `stm32h5xx_hal_flash.h` (`FLASH_KEY1`, `FLASH_KEY2`). The sequences follow the HAL's `FLASH_Program_QuadWord`, `FLASH_Erase_Sector` and `FLASH_WaitForLastOperation`, **not confirmed** at the pinned commit; `boot/hal/cortex-m/stm32h5/h5_flash.c`, tested by `test/unit/boot/test_boot_stm32h5_flash.c` |
| EEPROM | **verified on the board 2026-11-17**: the bench image, commit c97a2728, image sha256 `b7775ed0a97e0d4fcca7ca9fff099a30999fcf4d01b3640a82bd6ef93ecc7167`; write and read at 0x140 (8 bytes) and 0x2138 (the last 8); a 2-byte write at 0x144 over programmed bytes reads back with its neighbours kept (the sector rewrite); the values survive a reset and are in flash at 0x081FE000 and 0x081FFFF8; writes at 0x000 and 0x13F are refused as platform-reserved, 0x213C with 5 bytes and 0x2140 as out of range, more than 8 bytes as outside 1..8; a read at 0x000 returns the platform block: `TFW_EEPROM_SIZE` 0x2140. Addresses 0x000 to 0x13F are tfw-boot's span, read from the platform block; a write there is `TFW_ERR_RANGE` except the OTA confirm request, which keeps the generic Cortex-M store into the platform block's RAM alias, unchanged by this row, so a confirm does not yet reach flash. Addresses 0x140 to 0x213F are the user sector, the 8 KiB at `0x081FE000` (bank 2's last sector), written through the application's one flash write, `src/hal/cortex-m/stm32h5/ee_flash.c`, which refuses any span outside the user sector and runs the programming sequence above (`h5_flash.c`). tfw-boot's flash-interface driver, `boot_flash.c`, stays boot-only: no application links it (`scripts/test_stm32h5_boot_only_flash.py`). It is in bank 2 so its erase is read-while-write: the application keeps fetching from bank 1 and SysTick keeps counting, where an erase in bank 1 would stall every fetch and lose the stall's ticks. Power loss: a write that must change a programmed quad-word rewrites the whole sector, so a reset between its erase and the last restored quad-word loses every value in the user sector, not only the one written; a rewrite that does not read back whole is `TFW_ERR_INTERNAL` | `boards/stm32h573i-dk/board_profile.h` (`TFW_CM_USER_EE_BASE`, `TFW_CM_USER_EE_SIZE`); `src/hal/cortex-m/stm32h5/hal_eeprom.c`, host-tested by `test/unit/test_stm32h5_eeprom.c` |
| PWM | **verified on the board 2026-11-17**: the bench image, commit c97a2728, image sha256 `b7775ed0a97e0d4fcca7ca9fff099a30999fcf4d01b3640a82bd6ef93ecc7167`; measured on the pin by `PULSE_IN`, no jumper: duty 128 on D6 high 502 µs and low 498 µs, duty 64 high 251 µs, duty 192 on D5 high 753 µs, low 247 µs, so 999.9 Hz with 8-bit duty and the 64 MHz timer clock hold; duty 0 and 255 give no edge: TIM5, a general-purpose timer nothing else in the port drives, on the Arduino header's D6 (PH10, TIM5_CH1) and D5 (PH11, TIM5_CH2), alternate function 2. Eight-bit duty at about 1 kHz, as the AVR and the ESP32 give: ARR 254, PSC 250 from 64 MHz (999.9 Hz), PWM mode 1 with preload, so duty 0 is never high and 255 always is. The timer is set up on the first write, not at boot; a channel's pin goes to the timer on that channel's first write, and stop disables the channel and drives the pin low as an output. TIM5's clock is APB1's, 64 MHz with its prescaler at 1 | CMSIS header: `TIM5_BASE_NS`, `TIM_TypeDef`, `TIM_CR1_*`, `TIM_EGR_UG`, `TIM_CCMR1_OC1M*`/`OC1PE`/`OC2M`/`OC2PE`, `TIM_CCER_CC1E`/`CC2E`, `RCC_APB1LENR_TIM5EN`; HAL: `stm32h5xx_ll_tim.h` (`LL_TIM_OCMODE_PWM1`), `stm32h5xx_hal_gpio_ex.h` (`GPIO_AF2_TIM5`); the pins: ST's board configuration for the kit (STM32_open_pin_data, `PH10 ARD_D6 S_TIM5_CH1`, `PH11 ARD_D5 S_TIM5_CH2`). The timer clock equal to PCLK1 at prescaler 1 is **not confirmed** against RM0481. `src/hal/cortex-m/stm32h5/hal_pwm.c`, `h5_calc.h`, host-tested by `test/unit/test_stm32h5_pwm.c` |
| Pulse | **verified on the board 2026-11-17**: the bench image, commit c97a2728, image sha256 `b7775ed0a97e0d4fcca7ca9fff099a30999fcf4d01b3640a82bd6ef93ecc7167`; `PULSE_IN` and `PULSE_CAPTURE` read the PWM and tone pins above to the microsecond; a timeout of 65 ms or more is refused out of range, 50 ms is taken: `PULSE_IN` and `PULSE_CAPTURE` on any pin of the board's table, its level read from the port's input data register (IDR) as `tfw_hal_gpio_read` reads it, whatever the pin's mode; the time counted on SysTick, which counts the processor clock, 64 MHz, so 64 cycles to the microsecond. Polling, as the AVR's and the ESP32's are: an interrupt between two polls lengthens the interval it falls in | `src/hal/cortex-m/hal_pulse.c`, `src/hal/cortex-m/cm_cycles.h`, `src/hal/cortex-m/hal_tick.c` (SysTick's CLKSOURCE, the processor clock), `src/hal/cortex-m/stm32h5/hal_gpio.c`; host-tested by `test/unit/test_cm_pulse.c` |
| Tone | **verified on the board 2026-11-17**: the bench image, commit c97a2728, image sha256 `b7775ed0a97e0d4fcca7ca9fff099a30999fcf4d01b3640a82bd6ef93ecc7167`; 1000 Hz on D3 high 499 µs, 440 Hz high 1136 to 1137 µs by `PULSE_CAPTURE`; stop leaves the pin low with no edge: TIM3 channel 2 in output-compare toggle mode on the Arduino header's D3 (PB5, TIM3_CH2), alternate function 2. TIM3 is a 16-bit general-purpose timer nothing else in the port drives. The pin flips at each match, so a period is two matches; the prescaler and reload come from `tfw_h5_tone_psc_arr` (`h5_calc.h`). One tone at a time: a second start halts the counter (CEN clear), writes PSC, ARR and CCR2, reloads with UG and runs it again, so a counter past a shorter reload never wraps; stop forces the output inactive (low) and halts the counter. Pin 12 of the board's table, after A0 to A5, `TFW_TONE_PIN`. TIM3's clock is APB1's, 64 MHz with its prescaler at 1 | CMSIS header: `TIM3_BASE_NS` (APB1 + `0x0400`), `TIM_TypeDef`, `TIM_CR1_CEN`, `TIM_EGR_UG`, `TIM_CCMR1_OC2M`, `TIM_CCER_CC2E`, `RCC_APB1LENR_TIM3EN` (bit 1); HAL: `stm32h5xx_ll_tim.h` (`LL_TIM_OCMODE_TOGGLE`, `LL_TIM_OCMODE_FORCED_INACTIVE`), `stm32h5xx_hal_gpio_ex.h` (`GPIO_AF2_TIM3`, 2); the pin: Pin data, the kit's board file (`PB5.GPIO_Label=ARD_D3`, `PB5.Signal=S_TIM3_CH2`) and the GPIO modes file (PB5 `TIM3_CH2` as `GPIO_AF2_TIM3`). `src/hal/cortex-m/stm32h5/hal_tone.c`, `soc_regs.h`, host-tested by `test/unit/test_stm32h5_tone.c` |
| Instruction cache | ICACHE is off at reset and tfw-boot does not enable it, so a flash write leaves nothing stale to invalidate | ICACHE off at reset is **not confirmed** against RM0481 |

## ADC

ADC1, one polled conversion a read, on the Arduino header's six analog pins
(`src/hal/cortex-m/stm32h5/hal_adc.c`). Each source file's commit and SHA-256
is in [`../reference/README.md`](../reference/README.md).

| Pin | Header | MCU pin | ADC1 input | Source |
|---|---|---|---|---|
| 6 | A0 | PB0 | INP9 | Connector: `gpio-map`; pin table: `adc1_inp9_pb0` |
| 7 | A1 | PA4 | INP18 | Connector; pin table: `adc1_inp18_pa4` |
| 8 | A2 | PA0 | INP0 | Connector; pin table: `adc1_inp0_pa0` |
| 9 | A3 | PA5 | INP19 | Connector; pin table: `adc1_inp19_pa5` |
| 10 | A4 | PA6 | INP3 | Connector; pin table: `adc1_inp3_pa6` |
| 11 | A5 | PF12 | INP6 | Connector; pin table: `adc1_inp6_pf12` |

| | | Source |
|---|---|---|
| Kernel clock | HCLK (CCIPR5 ADCDACSEL 0), so no PLL starts and `TFW_CM_CPU_HZ` stays the core's: 64 MHz | `stm32h5xx_ll_rcc.h` `LL_RCC_ADCDAC_CLKSOURCE_HCLK` |
| ADC clock | the kernel clock over 2 (CCR CKMODE 0, PRESC 1): 32 MHz. The ADC's highest clock is in DS14053, not read, so the slower of the dividers that need no PLL is taken | `stm32h5xx_ll_adc.h` `LL_ADC_CLOCK_ASYNC_DIV2` |
| Sample time | 640.5 ADC clock cycles on every channel, the longest, so a high-impedance source settles: about 20 µs a sample | `stm32h5xx_ll_adc.h` `LL_ADC_SAMPLINGTIME_640CYCLES_5` |
| Bring-up | out of deep power-down, regulator on and 20 µs waited, single-ended calibration, 4 ADC clock cycles, ADEN then ADRDY; 12 bits, right aligned, single conversion, an overrun overwrites. INP0's pin switch (OR OP0) is on | HAL: `HAL_ADC_Init`, `ADC_Enable`, `HAL_ADCEx_Calibration_Start`; `stm32h5xx_ll_adc.h` `LL_ADC_DELAY_INTERNAL_REGUL_STAB_US`, `LL_ADC_DELAY_CALIB_ENABLE_ADC_CYCLES`, `LL_ADC_EnableChannel0_GPIO` |
| Poll bound | 10 ms on the calibration, ADRDY and EOC, past each; a miss is `TFW_ERR_TIMEOUT` | the calibration's length is RM0481's, not read, so the longer bound is taken |
| Reference | VREF+ only (`TFW_ADC_REF_VCC`); VREFBUF is not wired on this port | |
| Pin mode | the pins are analog only, but the GPIO commands reach them (they check the reserved and in-use flags, not `TFW_PIN_DIGITAL`). So every read first puts the channel's pin back to analog with no pull (MODER `0b11`, PUPDR `0b00`), and an output or pull-up set before it cannot drive or bias the input | `hal_adc.c`, tested by `test/unit/test_stm32h5_adc.c` |

**Run on the board 2026-11-17 (#541 review), at levels the board sets
itself.** No known voltage is wired to the header on the bench, so each
channel was read with `tfwctl analog` over the ST-LINK VCOM, on the bench
image of the run below (the `I2C=1` image, CAPS `analog`), at three levels:
floating, just after `mode out` and `write 1`, and just after `write 0`. The
read puts the pin back to analog with no pull, so the driven level is the
charge left on the pin and the board's trace, read once and then gone. Counts
are 12-bit, VREF+ reference:

| Channel | Floating | After high | After low | Next read |
|---|---|---|---|---|
| A0 (INP9) | 460, 212 | 2388 | 0 | 208, 210 |
| A1 (INP18) | 664, 599 | 2546 | 0 | 603, 596 |
| A2 (INP0) | 182, 183 | 3142 | 18 | 177, 190 |
| A3 (INP19) | 645, 555 | 2521 | 19 | 555, 551 |
| A4 (INP3) | 494, 378 | 2678 | 0 | 375, 382 |
| A5 (INP6) | 570, 393 | 2677 | 0 | 396, 395 |

So on every channel the converter is up (calibration, ADRDY and EOC within
the 10 ms bound: no timeout), each channel reads its own pin (it follows the
level set on that pin), INP0's OP0 switch connects PA0, and a pin driven low
reads at or near zero. The "after high" count is a held charge, not a known
voltage, so it shows only that a high level reads high.

**Not run: a known voltage on each of A0 to A5** reading as the expected
count (no source wired on the bench; issue to file: wire a divider or the
3V3/GND header pins to A0 to A5 and record the counts). `adc-ref internal`
is refused on this build, so VREFINT cannot stand in for it. Also not
confirmed against RM0481 and DS14053: the ADC's highest clock and so the
divider; the calibration's length; that 640.5 cycles is the sample time a
header source needs. The register model in `test/unit/test_stm32h5_adc.c`
checks the order and the values written.

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
| ST's secure world | **STM32Trust TEE Secure Manager** (STM32TRUSTEE-SM): a PSA-compliant secure world for Armv8-M, with secure boot, cryptography, internal trusted storage, initial attestation and firmware update. **Not taken** as this project's secure world | UM3254, *Secure Manager for STM32H573xx microcontrollers*, Rev 8, August 2025; section not confirmed. Feature list also on ST's STM32TRUSTEE-SM page |
| Secure Manager access kit | X-CUBE-SEC-M-H5, distributed under export-control conditions | ST's STM32TRUSTEE-SM page |
| Certification targets | PSA Certified Level 3 and SESIP 3 are the part's targets; the SESIP guidance is UM3238 | ST product page *STM32H573II*; UM3238, *STM32H573xx STM32TrusteeSM security guidance for SESIP 3 certification*, revision not confirmed |
| Factory device certificate | **not confirmed** (the `stm32h5` row states one; no ST document was read that says so) | |
| Hardware crypto (PKA, AES, HASH) | the default image does **not offer** it (board 2026-11-17: CAPS reports root of trust software and ecdsa-p256-sha256 only; the port uses the soft tier). The `CRYPTO=stm32h5` tier drives the HASH and the PKA (#544); its bench results are in "Hardware crypto on the kit", below. The AES is not driven | `src/crypto/stm32h5/` |

## Hardware crypto on the kit

`make firmware BOARD=stm32h573i-dk CRYPTO=stm32h5`, on the kit on 2026-11-17.
The kit's tfw-boot holds a bench key whose private half was not on the bench
host, so the image could not be signed for it. It was written to the
application slot alone with pyOCD (`load` at `0x08008000`, read back equal) and
started from the debugger at its own vector table, after tfw-boot had run. The
whole flash was read before the write and written back after it; the read-out
after matched the first one's SHA-256, and tfw-boot booted the board's prior
application. No option byte, RDP, TZEN, OTP or WRP was written and nothing was
mass-erased.

| Row | 2026-11-17 | Evidence |
|---|---|---|
| CAPS | **pass** | `tfwctl caps`: board id `0x43`, root of trust software (`TFW_CRYPTO_SOFTWARE`, as the tier states), algorithms `ecdsa-p256-sha256`; ping answers. `make hw-test` could not run its CAPS row: the harness has no pin facts for `stm32h573i-dk` (`peerBoards`) |
| PKA start (INITOK) | **fail, fixed in #544** | RCC_AHB2ENR's HASHEN, RNGEN and PKAEN set and PKA CR EN 1, but SR stayed 0: with the RNG's bus clock alone the PKA never reached INITOK, so every sign and verify answered from the soft code. With RCC_CR HSI48ON set and HSI48RDY up, INITOK rose at EN. The provider now starts the HSI48 first (`tfw_h5_hsi48_start`); with it, after boot RCC_CR shows HSI48RDY and PKA SR INITOK 1 |
| verify (PKA ECDSA P-256) | **not run** | no command of the unprovisioned image reaches `verify_operator` or `verify_image` (the attestation commands refuse unprovisioned, OTA is off in this build), and `make hw-test` has no row for this board |
| hash (HASH SHA-256) | **not run** | the same: no command of this image reaches `tfw_crypto_sha256` unprovisioned. HASH SR read `0x00110001` and CR 0 after boot: the peripheral is clocked and idle |
| PKA error flags, poll bound | **not observed** | no PKA operation ran on the kit |

## Upstream TF-M

As ADR #15 states it ("Platforms upstream at v2.3.1"): the STM32H573I-DK is an
upstream TF-M platform at **v2.3.1**; the NUCLEO-H563ZI is not.

- **TF-M v2.3.1, built only (#608).** `make tfm-image TFM_BOARD=stm32h573i-dk`
  (`mk/arch/tfm-targets.mk`, `scripts/tfm-build.sh`) builds BL2, the secure
  image (Profile Medium, Initial Attestation off, our DICE partition and
  tf-m-extras' Measured Boot on) and tf-m-extras' example non-secure image
  from TF-M's own `stm/stm32h573i_dk` port, out of tree in `TFM_WORK`. The
  images are built and never flashed. Running them needs TZEN=1 and the
  secure watermark: option bytes the owner sets. Nothing in this tree writes
  an option byte.

## Bench run

Run on 2026-11-17 (#535) at commit c97a2728 (main f2a88d8c with the pyOCD
read-back fix), TrustZone off (OPTSR2 TZEN `0xC3`, the factory state), no
option byte written. The board's factory record was taken first, kept on the
bench node, not in the repository.

- **Main as it is leaves the board silent.** `make boot-flash` then `make
  flash` write and read back equal, but tfw-boot has no platform block and no
  pinned key, so it halts before the application. Main has no provisioning
  step for a Cortex-M silicon board; that defect is for an issue.
- **The bench image.** The QEMU integration's signer
  (`test/integration/cortex-m/cm_image.c`), built for this board with its load
  addresses on the compiler line, pins a P-256 key in tfw-boot, signs the
  application and writes the platform block. The three, packed into one image
  at `0x08000000`, are written by `make boot-flash BOOT_FLASH_HEX=<image>`,
  which keeps the record check and the read-back. tfw-boot's P-256 check takes
  about 10 s at 64 MHz, then the application sends its boot frame.
- **The Ethernet probe (#579), 2026-11-17.** The bench image again, its
  application built with `ETH_PROBE=1`. The signer needs the board's load
  addresses and stage length on its compiler line, which `cortex-m-qemu-image`
  takes through `BOARD_DEFS`: `-DTFW_CM_LOAD_APP_AT=TFW_APP_REGION_BASE
  -DTFW_CM_LOAD_PLATFORM_AT=TFW_CM_PLATFORM_BASE
  -DTFW_OTA_STORE_STAGE_LEN=0x020000ul`. The target then stops on the missing
  `CM_QEMU_BOARD_ID.stm32h573i-dk`, so its objcopy, nm and `cm_image` steps
  were run by hand, and `boot.bin`, `app.bin` and `platform.bin` were packed at
  `BOOT_AT`, `APP_AT` and `PLATFORM_AT` into one hex for `make boot-flash
  BOOT_FLASH_HEX=`. After a debugger reset the console printed the `eth:` line,
  then the boot frame.
- **The rows were driven with `tfwctl`.** `make hw-test BOARD=stm32h573i-dk`
  cannot run here: the board has no row list (`boardImages`) and no pin facts
  (`peerBoards`), and its DTR reset does not reset the board through the
  ST-LINK VCOM, so its ping gets no reply. That is for an issue.
- CAPS: protocol 2.0, board id 0x43, 13 pins, 8 schedule slots, max payload 64,
  root of trust software, features gpio sched analog pwm tone pulse eeprom
  attest (and i2c on the `I2C=1` image), ecdsa-p256-sha256. `SYS` gives the
  last reset as unknown, after a debugger reset too; that is for an issue.

## Feature matrix

One line per `docs/SPEC.md` row that names this board; each claim is
verified (date, evidence), fails (what, an issue to file) or owner (a write
CA 583 forbids: option bytes or provisioning).

**`stm32h5`**

- Board as data, board id 0x43: verified 2026-11-17, CAPS board id 0x43.
- HSI 64 MHz clock: timer ratio only, 2026-11-17; the absolute frequency is
  pending a scope or counter. The PWM period (999.9 Hz) and the tone pitch were
  measured on the pins by `PULSE_CAPTURE`, which counts SysTick cycles and
  converts them with the assumed `TFW_CM_CPU_HZ`. The timers and SysTick all
  run from HSI, so an error in HSI scales the signal and its measurement alike:
  the run shows the timers' dividers are right relative to the core clock, not
  that the core clock is 64 MHz.
- ST-LINK console on USART1, 115200: verified 2026-11-17, boot frame, CAPS,
  `ping`.
- Watchdog: verified only in that the image runs for minutes without a reset;
  the 4 s period is not measured.
- GPIO: verified 2026-11-17, pin levels read through `PULSE_IN` and `read`.
- ADC, A0 to A5: verified 2026-11-17 at levels the board sets itself (a pin
  driven low reads 0 to 19 counts, driven high reads high); a known voltage is
  not run, none wired (issue to file).
- tfw-boot's flash writes: not runnable, no OTA row for this board; the shared
  sequence ran from the application's EEPROM.
- Cortex-M33 with TrustZone: owner (TZEN is an option byte; CA 583).
- Hardware crypto: the default image is the soft tier (CAPS offers it only);
  the `CRYPTO=stm32h5` tier drives the HASH and the PKA (#544) and is built in
  CI (#596).
- ST Secure Manager: not taken (ruling 935,
  [`../multi-arch/adr-mcu-dice-attestation.md`](../multi-arch/adr-mcu-dice-attestation.md));
  the secure world is upstream TF-M Profile Medium with our DICE layer as an
  Application RoT partition (ruling 1024, ADR #15 (b1)), not our own
  TrustZone-M firmware.
- Factory device certificate: not the identity root under ruling 935; whether
  the part carries one is a hardware fact, owner (provisioning writes; CA 583).
- Ethernet PHY and link: verified 2026-11-17, commit 889bddc7, the
  `ETH_PROBE=1` image's boot line `eth: phy addr=0 id=0007:C131 link=up`
  (a LAN8742A at MDIO address 0, link up; #579).
- Ethernet frames: verified 2026-11-17, commit b0226d8f, the `ETH_MAC=1`
  image (application sha256 `c3de81f3…2f31`, packed bench image
  `26ba05ce…a5d`): a whole DHCP exchange sent and received through the MAC
  and DMA (#581).
- DHCP lease: verified 2026-11-17, the same image's console line
  `eth: dhcp bound` with the leased address, about 15.6 s after reset (#581).
- IP stack: lwIP 2.2.1, NO_SYS, vendored unmodified (`third_party/lwip`).
- tfw-wire over TCP (board as server, port 21574; to re-run after fix-584k
  (outbound transport): the image now dials its network block's endpoint
  and listens on nothing): verified 2026-11-17,
  commit 1cf43fdf, the `ETH_MAC=1` image (application sha256
  `7702b4aa…c807`, packed bench image `445e0d16…dc0a`), from the bench host
  with `make hw-test BOARD=stm32h573i-dk PORT=tcp:<board>:21574` and PR
  #568's host transport (#582). The link is
  `src/hal/cortex-m/stm32h5/hal_eth_wire.c` over the lwIP listener
  `hal_eth_wire_tcp.c`, host-tested by `test/unit/test_h5_eth_wire.c`.
  The P4's row set over TCP, 2026-11-17, the same image:
  - pass: `Ping`, `PingAfterAMalformedBurst`, `Caps`, `Sys` (after a fresh
    boot), `Pin`, `Write`, `Timer`, `EEPROM`, `Analog`, `PWM`, `PulseIn`,
    `Tone`, `AppIsAnUnknownCommandWithoutAnApplication`,
    `AttestCSRReportsADeviceThatCannotIssueOne`,
    `ElementWithoutTheCapsBitIsRefusedAsUnsupported`,
    `SoftwareTierStillCannotIssueACertificate`, the I2C, SPI and 1-Wire
    refusals, `RefusesTheAuthSessionItsCapsLacks`,
    `RefusesAttestCSRItsCapsLacks`.
  - fails, as on the P4: `PingDeliversTheBootEventWithNoBootWait`, a `tcp:`
    connect resets nothing, so no boot event (PR #568's `partResets` leaves
    the row out over TCP); `Sys` after the malformed-burst row counts that
    row's 160 framer format errors (PR #568 reads a baseline first).
  - not runnable on this image: `I2CConfig` (I2C off by default),
    `OTAInfo` and `SoftTierOTAActivateWithoutOTAAuthIsAuthRequired` (no OTA
    stager, `OTA_STATUS` unknown).
- PR #584's `ETH_MAC=1` image on the board, 2026-11-18 (lane hw-584f,
  rulings 922 and 814): built at 8c687368 with `make BOARD=stm32h573i-dk
  ETH_MAC=1 cortex-m-dk-image` (size-check passes, application 38472
  bytes), bench.hex SHA-256 `b94fb6c0…0929`; `boot.bin`, `app.bin` and
  `platform.bin` written with `pyocd load -t stm32h573iikx -e sector
  --format bin` at the `addrs.env` addresses (pyOCD 0.46.1 refuses
  that `bench.hex` itself: two start-address records), no option byte written.
  Since #584's fix-584b the packer keeps only the boot's start-address
  record and checks there is one, with one end-of-file record, so the
  packed HEX loads directly; that load is not yet run on the board.
  The board is left on this image.
  - PHY ID (#579): verified 2026-11-18. This image prints no `eth: phy`
    line when it finds a PHY, so with the image running and the lease
    held the core was halted and MDIO read through MACMDIOAR/MACMDIODR at
    PHY address 0 (`pyocd cmd -c halt -c write32 … -c read32 …`, then
    `go`): PHYID1 `0x0007`, PHYID2 `0xC131`, the LAN8742A of the
    `ETH_PROBE=1` line above.
  - Link (#579): verified 2026-11-18, the same read: BMSR `0x782D`, bit 2
    (link up) set on the second read.
  - DHCP lease (#581): verified 2026-11-18, the console line `eth: dhcp
    bound` with a dev-VLAN lease, 16 s after a debugger reset; the address
    was taken from that line only, no network scan.
  - tfw-wire over TCP (#582; inbound listener, to re-run after fix-584k
    (outbound transport)): verified 2026-11-18, `make hw-test
    BOARD=stm32h573i-dk PORT=tcp:<the bound line's address>:21574
    HW_RESET='cmd:pyocd reset -t stm32h573iikx'`: the board's row
    selection for a `tcp:` port passes (`ok`, 93 s).
- The hw-test form for the outbound transport: `make
  hw-test BOARD=stm32h573i-dk PORT=tcp-listen:<host-port>`. The host
  listens on `<host-port>` (all interfaces; `<host>:<port>` binds one) and
  accepts the connection the board dials to the endpoint in its network
  block, so the block's peer and port must name the bench host and
  `<host-port>`. Each row opens a new listener and waits for the board's
  redial (at most 16 s after the last connection ended). With `HW_RESET`
  set, the listener opens before the reset, so the rebooted board's first
  dial is accepted. A `tcp-listen:` run selects the same rows as a `tcp:`
  run (no boot-event row). Host-tested by `sdk/go/internal/peer` and
  `TestTheDKRowsRunOverTheDevicesOutboundConnection` (tfwsim); not yet run
  on the board. A changed network block now applies at the next dial: the
  netif is reconciled with the block first (address, DHCP, MAC), and the
  lwIP and DHCP timers run while a signature is computed (#584).
- PR #584's outbound-transport image on the board, 2026-11-20 (lane
  hw-584g, slice 22 of #543): built at 0bb49390 with `make
  BOARD=stm32h573i-dk ETH_MAC=1 cortex-m-dk-image` (application 40952
  bytes, size-check passes).
  - App-partition-only flash: failed 2026-11-20. `app.bin`
    alone written with `pyocd load -e sector` at the `addrs.env` address;
    tfw-boot then refused it on every reset (status-only event, status 14,
    `TFW_ERR_CRYPTO`, no BOOT event): `cortex-m-dk-image` signs each build
    with a fresh key that only that build's `boot.bin` and `platform.bin`
    carry, so a later app cannot boot on an earlier build's boot. `boot.bin`,
    `app.bin` and `platform.bin` were then written together (sector erase,
    no option byte), and the board boots this image.
  - Unprovisioned: verified 2026-11-20, the BOOT event then the console
    line `eth: not provisioned` (no network block; the UART is served).
  - Network block: written 2026-11-20 over the UART with `tfwctl netcfg set
    --mac 00:00:00:00:00:00 --dhcp --endpoint <lane host>:7020`.
  - DHCP lease (#581): verified 2026-11-20, the console line `eth: dhcp
    bound` with a dev-VLAN lease about 16 s after a debugger reset. The
    image prints no PHY, link or dial line, so those are not seen on the
    UART in this run.
  - tfw-wire over TCP, outbound (#582, #584 fix-584k): failed 2026-11-20,
    `make hw-test BOARD=stm32h573i-dk PORT=tcp-listen:7020
    HW_RESET='cmd:pyocd reset -t stm32h573iikx'`: each selected row
    (`Caps`, `AppIsAnUnknownCommandWithoutAnApplication`,
    `AttestCSRReportsADeviceThatCannotIssueOne`,
    `SoftwareTierStillCannotIssueACertificate`,
    `RefusesAttestCSRItsCapsLacks`) fails with `no connection from the
    board: accept ... i/o timeout`, while the board resets and re-leases
    (`eth: dhcp bound` after each row's reset). Cause: the lane container's
    only interface is on the Docker bridge, with no published port and no
    dev-VLAN address, so the network block names no endpoint the board
    can reach. Not a firmware result; host networking was not changed. The
    issue naming the gap is for the coordinator to file (the lane's `gh`
    has no login). To re-run from a listener on the dev VLAN.
- Enrolling over Ethernet: CAPS and the attestation refusals verified over
  TCP 2026-11-17 (above); attestation itself is owner (provisioning; CA 583).

**`cortex-m`**

- Written through one debug-probe tool, record first, refusal without it:
  verified 2026-11-17 with pyOCD; STM32CubeProgrammer, the tool the row names,
  was not used.
- tfw-boot verifies a P-256 signature in software and boots the firmware:
  verified 2026-11-17, the bench image (above).
- A boot of the main image without a provisioning step: fails, tfw-boot halts
  silently (issue to file).
- `make hw-test` on the board: fails, no row list, no pin facts, no reset
  through the VCOM (issue to file).
- No option-byte, protection, lock, OTP or fuse operation: verified, none run.

**`ota`**

- Signed OTA on the Cortex-M33 (MCUboot, TF-M, PSA Firmware Update): not
  runnable, the board's tfw-boot row has no OTA; owner for the TF-M path
  (TrustZone; CA 583).

## What this page bears on

- **ADR #15 open question 2** (vendor secure worlds): closed by ruling 935;
  the Secure Manager (UM3254, export-controlled access kit) is not taken
  ([`../multi-arch/adr-mcu-dice-attestation.md`](../multi-arch/adr-mcu-dice-attestation.md)).
- **ADR #15 open question 1**: the upstream TF-M platform is this board, not
  the NUCLEO-H563ZI.
- **The `stm32h5` row's "enrolling over Ethernet"**: this board has Ethernet on
  board, so no W5x00 module is needed for it. With no hardware TCP/IP stack,
  an IP stack runs on the MCU; which one is the Ethernet ADR's open question 1
  ([`../multi-arch/adr-stm32h5-ethernet.md`](../multi-arch/adr-stm32h5-ethernet.md)).

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
- `TestFirmwareAttestRefusedUnprovisioned`: **verified** 2026-11-17 (#570)
  on the unprovisioned boot, run alone, 16 s from the probe reset; held from
  the bench boot's list. The earlier timeout (`command 0x60 did not respond
  after 5.006s`) was the precondition: the bench boot, `cm_image`'s
  `boot.bin`, carries a UDS, so the soft tier is provisioned and `attest`
  signs. Measured with `tfwctl -timeout 60s <vcom> attest <nonce>` on that
  image: evidence after 27.7 s, twice, against 2.0 s for `caps`, so about
  26 s of software P-256 at 64 MHz, so the hw suite's attest timeout on
  this board is 45 s (`attestTimeout` in `e2e_rows_test.go`), 5 s
  elsewhere. To run the row, build the signed set with `make
  cortex-m-dk-image BOARD=stm32h573i-dk`: the default build, signed by
  `cm_image` with the board's load addresses (the `BOARD_DEFS` of the
  Ethernet probe above), into `build/integration/stm32h573i-dk/bench/`.
  There `bench-unprovisioned.hex` packs `boot-unprovisioned.bin`, `app.bin`
  and `platform.bin` at the addresses in `addrs.env`, and `bench.hex` the
  same with `boot.bin`, the bench boot. Write the first with `make
  boot-flash BOARD=stm32h573i-dk BOOT_FLASH_HEX=<bench>/bench-unprovisioned.hex`
  (the packed HEX carries one start-address record, so it loads directly;
  the three blobs with `pyocd load -t stm32h573iikxq -e sector` also work), and
  run the row by name. On that boot
  `attest` answers "no provisioned identity" in the 2.0 s a `caps` takes.
  The board was left on the same set's `boot.bin` (provisioned).
  - 2026-11-20, ruling 1021, full three-image flash (lane hw-570): one
    `make BOARD=stm32h573i-dk cortex-m-dk-image` build at 17d109c9, `boot-unprovisioned.bin`, `app.bin` and
    `platform.bin` written with `pyocd load -t stm32h573iikx -e sector` at
    the `addrs.env` addresses (`stm32h573iikx` after `pyocd pack install`),
    then `pyocd reset`. The console sent the BOOT event (status 0) and no
    other bytes in 45 s. `caps` sent at the reset gets no reply in 60 s (the
    board does not read the UART before its boot frame); after the boot
    frame `caps` answers in 2.0 s, root of trust software, `attest` in its
    features. `attest` with a zero nonce: refused in 2.0 s, "device has no
    provisioned identity", far inside the 45 s board timeout; that refusal
    is the provisioning state read back (no platform identity: no UDS, so
    no CDI). The row, run alone with `TFW_HW_RESET=cmd:pyocd reset`:
    **PASS** in 16.1 s from the probe reset. No code change. The board was
    left on this build's unprovisioned set.
- `TestFirmwareRefusesI2CItsCapsLacks`: **verified** 2026-11-17 on the
  default image (below), run alone, 16 s from the probe reset. On the `I2C=1`
  build it fails by design (`this row needs a peer without CAPS feature i2c`).

The default image (#543 slice 3b, 2026-11-17): the #535 tip
(`row/535-stm32h573i-dk-hw`) built with no `I2C=1` (`make firmware`, `make
bootloader-check`), signed by `cm_image` as the bench image is, written with
`pyocd load -e sector` (boot, application and platform block at
`0x08000000`, `0x08008000` and `0x080FE000`), no option byte written. The
first 160 KiB read back with `savemem` have SHA-256
`ce822c89f7c8ea74b1f87b1820f8b9d5b6fd90513426d3e8c9f193946b87d028`, the
packed image's. The command: `make hw-test BOARD=stm32h573i-dk PORT=<vcom>
HW_RESET='cmd:pyocd reset -t stm32h573iikx -u <probe serial>'
HW_TEST_RUN='^TestFirmware<Row>$'`. The board is left on this image, so the
`I2C=1` rows below need that build written back first.
- `TestFirmwareI2C`, `TestFirmwareI2CConfig` (the `I2C=1` image's rows):
  verified 2026-11-17, `HW_TEST_RUN='^TestFirmware(I2C|I2CConfig)$'`.

## What is not verified

Apart from the hw-test rows above, the bench run (2026-11-17) checked the console, the timers' clock ratio to
SysTick (the absolute 64 MHz is pending a scope or counter), the PWM, the tone, the pulse timing, the EEPROM, the I2C NACK path and the ADC on A0 to A5 at
levels the board sets itself (see the ADC section); not a known voltage on the
ADC (none wired; issue to file), the watchdog's period or tfw-boot's own flash writes. Of the
`CRYPTO=stm32h5` image only CAPS and the PKA start have run on the board
("Hardware crypto on the kit"). The port's register
values are ST's source code, not RM0481. The rest of the
ST-sourced values above are from indexed summaries of UM3143 and UM3254, not
from the documents.

The tone ran on the board (2026-11-17), its pitch measured on the pin by
`PULSE_CAPTURE`, so TIM3 counts at the core clock's rate (SysTick's, from the
same HSI; the absolute 64 MHz is not measured). Not settled by the bench run:

- That UG with CEN clear loads PSC and ARR and zeroes CNT without a stray
  toggle of the output, and that the first toggle after a retarget comes one
  full half-period later.
- That forcing OC2M to inactive drives PB5 low at once while CC2E stays set.
- That PB5 reaches the Arduino header's D3 with no solder bridge or jumper
  to set: the board file names the label, not the kit's wiring; UM3143's
  solder-bridge table has not been read.
- The pitch measured with a scope or counter, not the board's own SysTick:
  not done.
- The TF-M images ("Upstream TF-M"): built, never flashed; the hardware run
  on this kit is pending (#608).
