/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ST NUCLEO-H563ZI (MB1404, #14, docs/boards/nucleo-h563zi.md): the generic
 * Cortex-M port on an STM32H563ZIT6, the same stm32h5 SoC as the
 * STM32H573I-DK without its crypto accelerators.
 *
 * The pins are the board's three user LEDs. Its user button, Ethernet and
 * the rest are not driven yet, so every other capability is off by file
 * selection, as on the STM32H573I-DK.
 *
 * The console is USART3, the ST-LINK/V3E Virtual COM port on PD8 TX / PD9
 * RX (src/hal/cortex-m/stm32h5/soc_console.h). The pins and the LEDs are
 * TO CONFIRM against UM3115, not held here.
 */
#ifndef TFW_BOARD_NUCLEO_H563ZI_H
#define TFW_BOARD_NUCLEO_H563ZI_H

#define TFW_BOARD_NAME "nucleo-h563zi"
#define TFW_BOARD_ID   0x47u   /* docs/boards/board-id-registry.md */

#define TFW_H5_CONSOLE_USART3 1

#define TFW_PIN_COUNT  3u
#define TFW_PORT_COUNT 3u      /* GPIOB, GPIOF and GPIOG, the LEDs' banks */
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

/* The system clock: the HSI undivided, every bus prescaler at 1
 * (src/hal/cortex-m/stm32h5/hal_soc.c). SysTick and the USART divisor count
 * it; the SoC asserts it is 64 MHz. */
#define TFW_CM_CPU_HZ 64000000uL

/* Flash geometry, as on the STM32H573I-DK: the STM32H563ZIT6 has the same
 * two 1 MiB banks of 8 KiB sectors. The application region is the code
 * budget, FLASH_BUDGET in profile.mk. */
#define TFW_BOOT_REGION_BASE  0x08000000uL
#define TFW_BOOT_REGION_SIZE  0x00008000uL   /* 32 KiB, BOOT_SECTION */
#define TFW_APP_REGION_BASE   0x08008000uL
#define TFW_APP_REGION_SIZE   0x00020000uL   /* 128 KiB, FLASH_BUDGET */
#define TFW_FLASH_TOTAL       0x08200000uL   /* the end of bank 2 */

/* The platform block: bank 1's last 8 KiB sector, as on the STM32H573I-DK. */
#define TFW_CM_PLATFORM_BASE  0x080FE000uL
#define TFW_CM_PLATFORM_SIZE  0x00002000uL

/* Board sizing deviations from config.h: none. */

#endif /* TFW_BOARD_NUCLEO_H563ZI_H */
