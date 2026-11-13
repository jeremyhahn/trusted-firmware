/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Raspberry Pi Pico 2 W, the RP2350A's Cortex-M33 core 0 (SPEC row rp2350,
 * docs/boards/rp2350-pico2w.md).
 *
 * The pins are the header GPIOs the port drives: GPIO2-22 and GPIO26-28.
 * GPIO0 and 1 are UART0, the byte link; GPIO23-25 and 29 are wired to the
 * CYW43439, and the board's LED is the radio's, not a GPIO, so the table has
 * no LED pin. PWM is on, one header GPIO per slice (board.c), and the
 * converter reads GPIO26-28 (ADC0-2); the buses are off by file selection
 * until a slice drives them.
 *
 * The board id is PROVISIONAL (boards/rp2350-pico2w/profile.mk).
 */
#ifndef TFW_BOARD_RP2350_PICO2W_H
#define TFW_BOARD_RP2350_PICO2W_H

#define TFW_BOARD_NAME "rp2350-pico2w"
#define TFW_BOARD_ID   0x44u   /* docs/boards/board-id-registry.md */

#define TFW_PIN_COUNT  24u
#define TFW_PORT_COUNT 1u      /* SIO's one bank, bit n GPIOn */
#define TFW_PWM_COUNT  8u
#define TFW_ADC_COUNT  3u      /* ADC0-2, GPIO26-28 (src/hal/cortex-m/rp2350/hal_adc.c) */
#define TFW_EEPROM_SIZE 0u

#define TFW_FEATURE_ANALOG  1
#define TFW_FEATURE_PWM     1
#define TFW_FEATURE_TONE    1
#define TFW_FEATURE_PULSE   0
#define TFW_FEATURE_EEPROM  0
/* I2C0 on GPIO4/5 (src/hal/cortex-m/rp2350/hal_bus.c), only in the I2C=1
 * row: built and host-tested, not run on a board. */
#if defined(TFW_CM_I2C)
#define TFW_FEATURE_I2C     1
#else
#define TFW_FEATURE_I2C     0
#endif
#define TFW_FEATURE_SPI     0
#define TFW_FEATURE_ONEWIRE 0

/* The tone's pin: logical 5, GP7, a plain SIO pin that TIMER1 alarm 0
 * toggles (hal_tone.c). Not a PWM, I2C0 or ADC pin. */
#define TFW_TONE_PIN   5u          /* GP7 */

/* The soft root: the UDS in tfw-boot's region, the CDI handed over in SRAM,
 * HMAC evidence (src/crypto/soft/). The OTP root is the owner's to rule on. */
#define TFW_FEATURE_ATTEST 1

/* clk_sys and clk_peri, both the 12 MHz crystal undivided
 * (src/hal/cortex-m/rp2350/hal_soc.c). SysTick, the UART divisors and the PWM divider count it. */
#define TFW_CM_CPU_HZ 12000000uL

/* Flash geometry for tfw-boot, as addresses in the XIP window: the regions are
 * not offsets from zero, so TFW_FLASH_TOTAL is the end of the 4 MiB part
 * (include/tfw/boot.h asserts they fit below it and do not overlap). The
 * application region is the code budget, FLASH_BUDGET in profile.mk. 4 MiB is
 * pico-sdk 2.1.1's pico2_w.h line 79 (profile.mk has the source); the map is
 * pinned by literal in test/unit/boot/test_boot_rp2350_map.c. */
#define TFW_BOOT_REGION_BASE  0x10000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_BASE   0x10008000uL
#define TFW_APP_REGION_SIZE   0x00020000uL   /* 128 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x10400000uL   /* the end of the 4 MiB flash */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): the flash's last 4 KiB sector, the part's erase
 * unit, which tfw-boot writes and the application reads through XIP. */
#define TFW_CM_PLATFORM_BASE  0x103FF000uL
#define TFW_CM_PLATFORM_SIZE  0x00001000uL

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_RP2350_PICO2W_H */
