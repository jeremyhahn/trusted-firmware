/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Nordic's nRF54L15 DK (PCA10156): the nRF54L15's application core, a
 * Cortex-M33, on the generic Cortex-M port (#161); SPEC row nrf54l15 (#17).
 *
 * The pins are the DK's four LEDs and four buttons (Zephyr's board
 * definition, boards/nordic/nrf54l15dk/nrf54l15dk_common.dtsi). The console
 * UARTE's pins, P1.4 and P1.5, are the SoC file's
 * (src/hal/cortex-m/nrf54l/hal_uart.c) and not in the table. Every other
 * capability is off by file selection until a slice drives it.
 *
 */
#ifndef TFW_BOARD_NRF54L15_DK_H
#define TFW_BOARD_NRF54L15_DK_H

#define TFW_BOARD_NAME "nrf54l15-dk"
#define TFW_BOARD_ID   0x45u   /* docs/boards/board-id-registry.md */

#define TFW_PIN_COUNT  8u
#define TFW_PORT_COUNT 3u      /* GPIO P0, P1 and P2 */
#define TFW_PWM_COUNT  0u
#define TFW_ADC_COUNT  0u
#define TFW_EEPROM_SIZE 0u

#define TFW_FEATURE_ANALOG  0
#define TFW_FEATURE_PWM     0
#define TFW_FEATURE_TONE    1
/* LED1, P1.10: the tone's TIMER20, GPIOTE20 and DPPIC20 reach port P1 only
 * (src/hal/cortex-m/nrf54l/hal_tone.c). */
#define TFW_TONE_PIN        1u
#define TFW_FEATURE_PULSE   0
#define TFW_FEATURE_EEPROM  0
/* I2C on TWIM21 (src/hal/cortex-m/nrf54l/hal_bus.c), SDA P1.8 and SCL P1.10,
 * only in the I2C=1 row (profile.mk): the element tiers' measurement. Those
 * pins are also BTN2 and LED1 in board.c; the bus takes them while it runs. */
#if defined(TFW_CM_I2C)
#define TFW_FEATURE_I2C     1
#else
#define TFW_FEATURE_I2C     0
#endif
#define TFW_FEATURE_SPI     0
#define TFW_FEATURE_ONEWIRE 0

/* The soft root: the UDS in tfw-boot's region, the CDI handed over in SRAM,
 * HMAC evidence (src/crypto/soft/). The KMU-held root is a later slice. */
#define TFW_FEATURE_ATTEST 1

/* The CPU clock: the PLL at 128 MHz, which src/hal/cortex-m/nrf54l/hal_soc.c
 * selects at reset as the vendor's SystemInit does (nrfx MDK system_nrf54l.c).
 * SysTick counts it. The UARTE's baud rate is not derived from it: the
 * peripheral runs at 16 MHz whatever the CPU does. */
#define TFW_CM_CPU_HZ 128000000uL

/* Flash geometry for tfw-boot, as addresses (include/tfw/boot.h asserts they
 * fit below TFW_FLASH_TOTAL and do not overlap). The code bank is RRAM from
 * address 0. The application region is the code budget, FLASH_BUDGET in
 * profile.mk. */
#define TFW_BOOT_REGION_BASE  0x00000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_BASE   0x00008000uL
#define TFW_APP_REGION_SIZE   0x00020000uL   /* 128 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x0017D000uL   /* the end of RRAM, 1524 KiB */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): the last 4 KiB of RRAM, which tfw-boot writes and the
 * application reads. */
#define TFW_CM_PLATFORM_BASE  0x0017C000uL
#define TFW_CM_PLATFORM_SIZE  0x00001000uL

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_NRF54L15_DK_H */
