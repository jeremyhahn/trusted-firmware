/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ST STM32H573I-DK Discovery kit (#14, docs/boards/stm32h573i-dk.md): the
 * generic Cortex-M port on an STM32H573IIK3Q.
 *
 * The pins are the kit's four user LEDs, the Arduino header's D6 and D5,
 * the PWM pins (TIM5, src/hal/cortex-m/stm32h5/hal_pwm.c; built and
 * host-tested, not yet run on the board), and its six analog inputs, on ADC1
 * (src/hal/cortex-m/stm32h5/hal_adc.c). The kit's buses, Ethernet and secure
 * element are not driven yet, so every other capability is off by file
 * selection, as on mps2-an505.
 *
 * The board id is PROVISIONAL (boards/stm32h573i-dk/profile.mk).
 */
#ifndef TFW_BOARD_STM32H573I_DK_H
#define TFW_BOARD_STM32H573I_DK_H

#define TFW_BOARD_NAME "stm32h573i-dk"
#define TFW_BOARD_ID   0x43u   /* docs/boards/board-id-registry.md */

#define TFW_PIN_COUNT  13u
#define TFW_PORT_COUNT 5u      /* GPIOF, GPIOI (LEDs), GPIOH (D5/D6), GPIOA, GPIOB */
#define TFW_PWM_COUNT  2u
/* TFW_ADC_COUNT is the HAL's range-check bound, one past the highest ADC1
 * input a pin uses: A3 is INP19. */
#define TFW_ADC_COUNT  20u
/* tfw-boot's span (TFW_BOOT_EE_LEN, include/tfw/boot.h, 0x140) and the user
 * sector after it, TFW_CM_USER_EE_SIZE below (hal_eeprom.c asserts the sum). */
#define TFW_EEPROM_SIZE 0x2140u

#define TFW_FEATURE_ANALOG  1
#define TFW_FEATURE_PWM     1
#define TFW_FEATURE_TONE    1
/* Tone is TIM3 channel 2 toggling the Arduino header's D3, PB5
 * (src/hal/cortex-m/stm32h5/hal_tone.c; built and host-tested, not yet run
 * on the board). */
#define TFW_TONE_PIN   12u
#define TFW_FEATURE_PULSE   0
#define TFW_FEATURE_EEPROM  1
/* I2C4 on PB8/PB9 (src/hal/cortex-m/stm32h5/hal_bus.c), on only in the I2C=1
 * row (profile.mk): built and host-tested against a register model, not yet
 * run on the kit. */
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

/* The system clock: the HSI undivided, every bus prescaler at 1
 * (src/hal/cortex-m/stm32h5/hal_soc.c). SysTick and the USART divisor count
 * it; the SoC asserts it is 64 MHz. */
#define TFW_CM_CPU_HZ 64000000uL

/* Flash geometry for tfw-boot, as addresses: the regions are not offsets from
 * zero, so TFW_FLASH_TOTAL is the end of the flash both sit in
 * (include/tfw/boot.h asserts they fit below it and do not overlap). The
 * application region is the code budget, FLASH_BUDGET in profile.mk. */
#define TFW_BOOT_REGION_BASE  0x08000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_BASE   0x08008000uL
#define TFW_APP_REGION_SIZE   0x00020000uL   /* 128 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x08200000uL   /* the end of bank 2 */

/* The platform block, the 192 bytes the AVR keeps in EEPROM
 * (include/tfw/boot.h): bank 1's last 8 KiB sector, one erase unit of its
 * own, which tfw-boot writes and the application reads. */
#define TFW_CM_PLATFORM_BASE  0x080FE000uL
#define TFW_CM_PLATFORM_SIZE  0x00002000uL

/* The application's own EEPROM: the 8 KiB sector directly below the platform
 * block's (bank 1's sector 126), one erase unit of its own, so a write to it
 * never erases tfw-boot's sector (src/hal/cortex-m/stm32h5/hal_eeprom.c). The
 * sector is TFW_H5_FLASH_SECTOR (stm32h5xx_hal_flash_ex.h's FLASH_SECTOR_SIZE,
 * src/hal/cortex-m/stm32h5/soc_regs.h). No slot, budget or record of
 * profile.mk reaches it: the application slot ends at 0x08028000, and the
 * linker script (mk/arch/cortex-m/stm32h5.ld) places nothing outside the
 * slot. */
#define TFW_CM_USER_EE_BASE   (TFW_CM_PLATFORM_BASE - TFW_CM_USER_EE_SIZE)
#define TFW_CM_USER_EE_SIZE   0x00002000uL

_Static_assert(TFW_CM_USER_EE_BASE + TFW_CM_USER_EE_SIZE == TFW_CM_PLATFORM_BASE,
               "the user EEPROM sector does not end at the platform block");
_Static_assert(TFW_CM_USER_EE_BASE >= TFW_APP_REGION_BASE + TFW_APP_REGION_SIZE,
               "the user EEPROM sector overlaps the application slot");

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_STM32H573I_DK_H */
