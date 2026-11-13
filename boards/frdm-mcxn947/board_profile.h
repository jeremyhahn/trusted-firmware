/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * NXP FRDM-MCXN947: core 0 of the MCXN947 on the generic Cortex-M port, SoC
 * directory mcxn94x (SPEC row mcxn947, docs/boards/frdm-mcxn947.md).
 *
 * The pins are the board's RGB LED and its two user switches (FRDM-MCXN947
 * Board User Manual UM12018, the SDK's boards/frdmmcxn947/board.h), the
 * Arduino header's A2..A5 on LPADC0, and the tone's pin, P3_19. The red LED's
 * pin is also CTIMER0's PWM output. ADC1, the other timers, SPI and 1-Wire
 * are not driven by this port yet, so every other capability is off by file
 * selection. The EdgeLock subsystem is
 * not driven at all: the soft root is the board's only tier.
 *
 * The board id is PROVISIONAL (boards/frdm-mcxn947/profile.mk).
 */
#ifndef TFW_BOARD_FRDM_MCXN947_H
#define TFW_BOARD_FRDM_MCXN947_H

#define TFW_BOARD_NAME "frdm-mcxn947"
#define TFW_BOARD_ID   0x46u   /* docs/boards/board-id-registry.md */

#define TFW_PIN_COUNT  9u
#define TFW_PORT_COUNT 3u      /* GPIO0, GPIO1 and GPIO3, the banks the pins sit in */
#define TFW_PWM_COUNT  1u      /* CTIMER0 match 0 on P0_10, the red LED */
#define TFW_ADC_COUNT  64u     /* LPADC0 inputs: channel 0..31, + 32 for side B */
#define TFW_EEPROM_SIZE 0u

#define TFW_FEATURE_ANALOG  1
#define TFW_FEATURE_PWM     1
#define TFW_FEATURE_TONE    1
#define TFW_FEATURE_PULSE   0
#define TFW_FEATURE_EEPROM  0
/* I2C on LP_FLEXCOMM2 (src/hal/cortex-m/mcxn94x/hal_bus.c) only in the I2C=1
 * row (profile.mk): built and measured, not run on the board. */
#if defined(TFW_CM_I2C)
#define TFW_FEATURE_I2C     1
#else
#define TFW_FEATURE_I2C     0
#endif
#define TFW_FEATURE_SPI     0
#define TFW_FEATURE_ONEWIRE 0

/* The soft root: the UDS in tfw-boot's region, the CDI handed over in SRAM,
 * HMAC evidence (src/crypto/soft/). */
#define TFW_FEATURE_ATTEST 1

/* The core and bus clock: FRO12M, the 12 MHz internal oscillator, as main
 * clock with the AHB divider at 1 (src/hal/cortex-m/mcxn94x/hal_soc.c). The
 * SDK's BOARD_BootClockFRO12M runs the same tree. SysTick counts it, and the
 * host link's LP_FLEXCOMM takes the same oscillator as its functional clock. */
#define TFW_CM_CPU_HZ 12000000uL

/* The host link: LP_FLEXCOMM4 as LPUART4 on P1_8 (RX, FC4_P0) and P1_9 (TX,
 * FC4_P1), pin function 2, the MCU-Link's virtual COM port. The SDK names
 * LPUART4 the debug console; the pin function is to confirm against UM12018
 * and the SDK's pin_mux.c (docs/boards/frdm-mcxn947.md). */
#define TFW_MCXN_LINK_PORT    1u
#define TFW_MCXN_LINK_RX_BIT  8u
#define TFW_MCXN_LINK_TX_BIT  9u
#define TFW_MCXN_LINK_PIN_FN  2u

/* Tone: CTIMER2's match 1 toggling CT2_MAT1 on P3_19, the mikroBUS socket's
 * PWM pin, pin function 4, pin 8 (src/hal/cortex-m/mcxn94x/hal_tone.c). The
 * pinned citations and the checks left are docs/boards/frdm-mcxn947.md,
 * "Tone". */
#define TFW_TONE_PIN          8u   /* P3_19 */
#define TFW_MCXN_TONE_PIN_FN  4u

/* The I2C bus: LP_FLEXCOMM2 as LPI2C2 on P4_0 (SDA, FC2_P0) and P4_1 (SCL,
 * FC2_P1), pin function 2, open drain with the pull-up on: the SDK's
 * LPI2C2_InitPins (driver_examples/lpi2c/polling_b2b/master pin_mux.c), and
 * board.h's LPI2C2 for the board's accelerometer and codec. Which header
 * pins carry them is to confirm against UM12018. */
#define TFW_MCXN_I2C_PORT     4u
#define TFW_MCXN_I2C_SDA_BIT  0u
#define TFW_MCXN_I2C_SCL_BIT  1u
#define TFW_MCXN_I2C_PIN_FN   2u

/* PWM: CTIMER0 in PWM mode, match 0 on P0_10 (CT0_MAT0, pin function 4, the
 * SDK example driver_examples/ctimer/simple_pwm's pin_mux.c), the red LED's
 * pin, so the LED dims (active low: a high duty is a dim LED). The function is
 * the one every row of boards/frdm-mcxn947/board.c's PWM table takes
 * (src/hal/cortex-m/mcxn94x/hal_pwm.c). */
#define TFW_MCXN_PWM_PIN_FN   4u

/* Flash geometry for tfw-boot, as addresses: the regions are not offsets from
 * zero, so TFW_FLASH_TOTAL is the end of the flash span both sit in
 * (include/tfw/boot.h asserts they fit below it and do not overlap). The
 * application region is the code budget, FLASH_BUDGET in profile.mk. */
#define TFW_BOOT_REGION_BASE  0x10000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_BASE   0x10008000uL
#define TFW_APP_REGION_SIZE   0x00020000uL   /* 128 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x10200000uL   /* the end of the 2 MiB flash */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): the last 8 KiB sector of the flash, which tfw-boot
 * writes and the application reads. The sector size is to confirm against the
 * reference manual's flash chapter. */
#define TFW_CM_PLATFORM_BASE  0x101FE000uL
#define TFW_CM_PLATFORM_SIZE  0x00002000uL

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_FRDM_MCXN947_H */
