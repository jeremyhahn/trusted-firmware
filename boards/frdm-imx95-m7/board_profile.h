/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * NXP FRDM-IMX95-PRO, the i.MX 95's Cortex-M7 (SPEC row `imx95`): a board of
 * the generic Cortex-M port. The board is NXP UM12527 ("FRDM-IMX95-PRO Board
 * User Manual", Rev. 1.0), the SoC NXP IMX95RM ("i.MX 95 Applications
 * Processor Reference Manual"), which this project does not hold: every SoC
 * fact here and in src/hal/cortex-m/imx95/ is TO CONFIRM against it.
 *
 * The byte link is LPUART3, the M7's debug UART on NXP's i.MX 95 boards (TO
 * CONFIRM against UM12527, the debug UART section). The pins are two RGPIO
 * lines and every other capability is off by file selection. The System
 * Manager owns the pin mux (IOMUXC) and assigns pins and peripherals to the
 * M7, so the HAL muxes nothing: a pin the configuration did not give the M7
 * does not move.
 *
 * The board id is PROVISIONAL (boards/frdm-imx95-m7/profile.mk).
 */
#ifndef TFW_BOARD_FRDM_IMX95_M7_H
#define TFW_BOARD_FRDM_IMX95_M7_H

#define TFW_BOARD_NAME "frdm-imx95-m7"
#define TFW_BOARD_ID   0x41u   /* provisional */

#define TFW_PIN_COUNT  2u
#define TFW_PORT_COUNT 5u      /* GPIO1 to GPIO5 */
#define TFW_PWM_COUNT  0u
#define TFW_ADC_COUNT  0u
#define TFW_EEPROM_SIZE 0u

#define TFW_FEATURE_ANALOG  0
#define TFW_FEATURE_PWM     0
#define TFW_FEATURE_TONE    0
#define TFW_FEATURE_PULSE   0
#define TFW_FEATURE_EEPROM  0
#define TFW_FEATURE_I2C     0
#define TFW_FEATURE_SPI     0
#define TFW_FEATURE_ONEWIRE 0

/* The soft root, as on mps2-an505, until the EdgeLock enclave tier: the UDS
 * in tfw-boot's region, the CDI handed over in the DTCM. */
#define TFW_FEATURE_ATTEST 1

/* The M7's core clock, which SysTick counts. The System Manager configuration
 * sets it; 800 MHz is the part's M7 maximum (NXP IMX95CEC, the i.MX 95 data
 * sheet; TO CONFIRM against the configuration trusted-imx95 ships). */
#define TFW_CM_CPU_HZ 800000000uL

/* LPUART3's functional clock root, the 24 MHz oscillator (TO CONFIRM: the
 * System Manager configuration sets the root). The baud divisor counts it. */
#define TFW_IMX95_LPUART_CLK_HZ 24000000uL

/* Flash geometry for tfw-boot, as addresses in the M7's ITCM. TFW_FLASH_TOTAL
 * is the ITCM's end (include/tfw/boot.h asserts both regions fit below it and
 * do not overlap). The application region is the code budget, FLASH_BUDGET. */
#define TFW_BOOT_REGION_BASE  0x00000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_BASE   0x00008000uL
#define TFW_APP_REGION_SIZE   0x00030000uL   /* 192 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x00040000uL   /* 256 KiB of ITCM */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): the last 4 KiB of the ITCM. TCM: it survives a reset
 * and a reload, not a power cycle. */
#define TFW_CM_PLATFORM_BASE  0x0003F000uL
#define TFW_CM_PLATFORM_SIZE  0x00001000uL

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_FRDM_IMX95_M7_H */
