/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Arm MPS2+ AN505 under QEMU (`-M mps2-an505`): the generic Cortex-M port's
 * reference board (#161, docs/cortex-m/adr-reference-part-and-emulation.md).
 *
 * A test target, like esp32-qemu. QEMU models the FPGA's two user LEDs and no
 * switch, no converter, no PWM and no bus this port drives, so the pins are
 * the two LEDs and every other capability is off by file selection.
 *
 * The board id is PROVISIONAL (boards/mps2-an505/profile.mk).
 */
#ifndef TFW_BOARD_MPS2_AN505_H
#define TFW_BOARD_MPS2_AN505_H

#define TFW_BOARD_NAME "mps2-an505"
#define TFW_BOARD_ID   0x40u   /* docs/boards/board-id-registry.md */

#define TFW_PIN_COUNT  2u
#define TFW_PORT_COUNT 1u      /* the FPGA I/O block's LED register */
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

/* The soft root: the UDS in tfw-boot's region, the CDI handed over in SRAM,
 * HMAC evidence (src/crypto/soft/). */
#define TFW_FEATURE_ATTEST 1

/* The system clock the FPGA image runs the core and the APB at: QEMU's
 * an505 sysclk_frq and apb_periph_frq. SysTick and the UART divisor count it. */
#define TFW_CM_CPU_HZ 20000000uL

/* Flash geometry for tfw-boot, as addresses: on this part the regions are not
 * offsets from zero, so TFW_FLASH_TOTAL is the end of the address span both
 * sit in (include/tfw/boot.h asserts they fit below it and do not overlap).
 * The application region is the code budget, FLASH_BUDGET in profile.mk. */
#define TFW_BOOT_REGION_BASE  0x10000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_BASE   0x38000000uL
#define TFW_APP_REGION_SIZE   0x00020000uL   /* 128 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x38400000uL   /* the end of SSRAM-2 */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): the last 4 KiB of the code bank, which tfw-boot
 * writes and the application reads. */
#define TFW_CM_PLATFORM_BASE  0x383FF000uL
#define TFW_CM_PLATFORM_SIZE  0x00001000uL

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_MPS2_AN505_H */
