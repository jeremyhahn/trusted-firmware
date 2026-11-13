/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 peripherals this port drives, at their Non-secure aliases: the
 * part runs with TrustZone disabled (TZEN, the factory option byte), and
 * with TZEN clear every address is the Non-secure one. Included by the
 * application's SoC files beside it and by tfw-boot's
 * (boot/hal/cortex-m/stm32h5/).
 *
 * Every address, offset and bit is ST's CMSIS device header for the part,
 * STMicroelectronics/cmsis-device-h5 Include/stm32h573xx.h at 884b8dc7
 * (docs/boards/stm32h573i-dk.md cites each), not RM0481, which could not be
 * retrieved. The register names are the header's.
 *
 *   RCC     reset and clock control: the HSI divider, the bus clock enables
 *   FLASH   the flash interface's access control (wait states), and its
 *           program and erase registers, which only tfw-boot drives
 *   GPIOx   banks A to I, one 1 KiB block each from GPIOA
 *   USART1  the byte link (the board's ST-LINK Virtual COM port)
 *   IWDG    the independent watchdog, clocked by the LSI
 *   TIM5    the PWM's timer (hal_pwm.c)
 */
#ifndef TFW_CM_STM32H5_REGS_H
#define TFW_CM_STM32H5_REGS_H

#include "cm_regs.h"

/* ---- RCC, AHB3PERIPH_BASE_NS + 0x0C00 */
#define TFW_H5_RCC_BASE        0x44020C00u
#define TFW_H5_RCC_CR          TFW_CM_REG(TFW_H5_RCC_BASE + 0x00u)
#define TFW_H5_RCC_AHB2ENR     TFW_CM_REG(TFW_H5_RCC_BASE + 0x8Cu)
#define TFW_H5_RCC_APB2ENR     TFW_CM_REG(TFW_H5_RCC_BASE + 0xA4u)
#define TFW_H5_RCC_RSR         TFW_CM_REG(TFW_H5_RCC_BASE + 0xF4u)   /* soc_reset.c */

#define TFW_H5_RCC_CR_HSIDIV   (0x3u << 3)    /* HSI / 2^HSIDIV */
#define TFW_H5_RCC_CR_HSIDIVF  (0x1u << 5)    /* the divider has switched */
#define TFW_H5_RCC_APB2ENR_USART1EN (0x1u << 14)

/* ---- FLASH interface, AHB1PERIPH_BASE_NS + 0x2000 */
#define TFW_H5_FLASH_ACR       TFW_CM_REG(0x40022000u)
#define TFW_H5_FLASH_ACR_LATENCY 0xFu

/* The Non-secure programming registers (FLASH_TypeDef: NSKEYR 0x04, NSSR
 * 0x20, NSCR 0x28, NSCCR 0x30) and their bits (FLASH_SR_*, FLASH_CR_*,
 * FLASH_CCR_*). The keys are stm32h5xx_hal_flash.h's FLASH_KEY1/FLASH_KEY2.
 * To confirm against RM0481 (docs/boards/stm32h573i-dk.md). */
#define TFW_H5_FLASH_BASE      0x40022000u
#define TFW_H5_FLASH_NSKEYR_OFF 0x04u
#define TFW_H5_FLASH_NSSR_OFF  0x20u
#define TFW_H5_FLASH_NSCR_OFF  0x28u
#define TFW_H5_FLASH_NSCCR_OFF 0x30u
#define TFW_H5_FLASH_KEY1      0x45670123u
#define TFW_H5_FLASH_KEY2      0xCDEF89ABu
#define TFW_H5_FLASH_SR_BSY    (0x1u << 0)
#define TFW_H5_FLASH_SR_WBNE   (0x1u << 1)
#define TFW_H5_FLASH_SR_DBNE   (0x1u << 3)
#define TFW_H5_FLASH_SR_EOP    (0x1u << 16)
#define TFW_H5_FLASH_SR_WRPERR (0x1u << 17)
#define TFW_H5_FLASH_SR_ERRORS (0x7Fu << 17)  /* WRPERR to OPTCHANGEERR */
#define TFW_H5_FLASH_CR_LOCK   (0x1u << 0)
#define TFW_H5_FLASH_CR_PG     (0x1u << 1)
#define TFW_H5_FLASH_CR_SER    (0x1u << 2)
#define TFW_H5_FLASH_CR_STRT   (0x1u << 5)
#define TFW_H5_FLASH_CR_SNB_POS 6u
#define TFW_H5_FLASH_CR_SNB    (0x7Fu << 6)
#define TFW_H5_FLASH_CR_BKSEL  (0x1u << 31)
#define TFW_H5_FLASH_SECTOR    0x2000u        /* 8 KiB, FLASH_SECTOR_SIZE */
#define TFW_H5_FLASH_BANK_SIZE 0x00100000u    /* FLASH_BANK_SIZE, 128 sectors */
#define TFW_H5_FLASH_QWORD     16u            /* the programming unit, 128 bits */
#define TFW_H5_FLASH_MEM       0x08000000u    /* FLASH_BASE_NS, bank 1's first byte */

/* ---- GPIO, AHB2PERIPH_BASE_NS: GPIOA at its base, then GPIOB to GPIOI at
 * 0x400 apart (GPIOF 0x1400, GPIOI 0x2000). The bank index is the board
 * table's port row (src/hal/cortex-m/hal_types.h), 0 for A to 8 for I, and is
 * also the bank's bit in RCC_AHB2ENR (GPIOAEN 0, GPIOFEN 5, GPIOIEN 8). */
#define TFW_H5_GPIOA_BASE      0x42020000u
#define TFW_H5_GPIO_STRIDE     0x400u
#define TFW_H5_GPIO_BANKS      9u
#define TFW_H5_GPIO_PINS       16u
#define TFW_H5_GPIO_MODER(b)   TFW_CM_REG((b) + 0x00u)
#define TFW_H5_GPIO_PUPDR(b)   TFW_CM_REG((b) + 0x0Cu)
#define TFW_H5_GPIO_IDR(b)     TFW_CM_REG((b) + 0x10u)
#define TFW_H5_GPIO_BSRR(b)    TFW_CM_REG((b) + 0x18u)
#define TFW_H5_GPIO_AFRH(b)    TFW_CM_REG((b) + 0x24u)   /* AFR[1]: pins 8-15 */

/* MODER and PUPDR fields, two bits per pin. */
#define TFW_H5_MODE_INPUT      0x0u
#define TFW_H5_MODE_OUTPUT     0x1u
#define TFW_H5_MODE_AF         0x2u
#define TFW_H5_MODE_ANALOG     0x3u   /* the reset state of every pin but the debug port's */
#define TFW_H5_PULL_NONE       0x0u
#define TFW_H5_PULL_UP         0x1u

/* ---- USART1, APB2PERIPH_BASE_NS + 0x3800 */
#define TFW_H5_USART1_BASE     0x40013800u
#define TFW_H5_USART_CR1       TFW_CM_REG(TFW_H5_USART1_BASE + 0x00u)
#define TFW_H5_USART_BRR       TFW_CM_REG(TFW_H5_USART1_BASE + 0x0Cu)
#define TFW_H5_USART_ISR       TFW_CM_REG(TFW_H5_USART1_BASE + 0x1Cu)
#define TFW_H5_USART_ICR       TFW_CM_REG(TFW_H5_USART1_BASE + 0x20u)
#define TFW_H5_USART_RDR       TFW_CM_REG(TFW_H5_USART1_BASE + 0x24u)
#define TFW_H5_USART_TDR       TFW_CM_REG(TFW_H5_USART1_BASE + 0x28u)

/* FIFOEN stays 0, so RXNE and TXE are the one-byte flags. */
#define TFW_H5_USART_CR1_UE     (0x1u << 0)
#define TFW_H5_USART_CR1_RE     (0x1u << 2)
#define TFW_H5_USART_CR1_TE     (0x1u << 3)
#define TFW_H5_USART_CR1_RXNEIE (0x1u << 5)
#define TFW_H5_USART_CR1_TXEIE  (0x1u << 7)
#define TFW_H5_USART_ISR_ORE    (0x1u << 3)
#define TFW_H5_USART_ISR_RXNE   (0x1u << 5)
#define TFW_H5_USART_ISR_TC     (0x1u << 6)
#define TFW_H5_USART_ISR_TXE    (0x1u << 7)
#define TFW_H5_USART_ICR_ORECF  (0x1u << 3)

#define TFW_H5_USART1_IRQ      58u            /* USART1_IRQn */

/* USART1's pins on the STM32H573I-DK, the ST-LINK Virtual COM port: PA9 TX,
 * PA10 RX, alternate function 7 (ST's BSP, stm32h573i_discovery.h). */
#define TFW_H5_VCP_BANK        0u             /* GPIOA */
#define TFW_H5_VCP_TX_PIN      9u
#define TFW_H5_VCP_RX_PIN      10u
#define TFW_H5_VCP_AF          7u

/* ---- TIM5, APB1PERIPH_BASE_NS + 0x0C00: the PWM's timer (hal_pwm.c), a
 * general-purpose timer nothing else here drives. Offsets are TIM_TypeDef's
 * (CR1 0x00, EGR 0x14, CCMR1 0x18, CCMR2 0x1C, CCER 0x20, PSC 0x28, ARR 0x2C,
 * CCR1 0x34 to CCR4 0x40), its enable RCC_APB1LENR (0x9C) bit TIM5EN 3. It
 * counts the APB1 timer clock, TFW_CM_CPU_HZ with the bus prescaler at 1. */
#define TFW_H5_RCC_APB1LENR    TFW_CM_REG(TFW_H5_RCC_BASE + 0x9Cu)
#define TFW_H5_RCC_APB1LENR_TIM5EN (0x1u << 3)
#define TFW_H5_TIM5_BASE       0x40000C00u
#define TFW_H5_TIM_CR1         TFW_CM_REG(TFW_H5_TIM5_BASE + 0x00u)
#define TFW_H5_TIM_EGR         TFW_CM_REG(TFW_H5_TIM5_BASE + 0x14u)
#define TFW_H5_TIM_CCMR1       TFW_CM_REG(TFW_H5_TIM5_BASE + 0x18u)
#define TFW_H5_TIM_CCMR2       TFW_CM_REG(TFW_H5_TIM5_BASE + 0x1Cu)
#define TFW_H5_TIM_CCER        TFW_CM_REG(TFW_H5_TIM5_BASE + 0x20u)
#define TFW_H5_TIM_PSC         TFW_CM_REG(TFW_H5_TIM5_BASE + 0x28u)
#define TFW_H5_TIM_ARR         TFW_CM_REG(TFW_H5_TIM5_BASE + 0x2Cu)
#define TFW_H5_TIM_CCR(ch)     TFW_CM_REG(TFW_H5_TIM5_BASE + 0x30u + 4u * (uint32_t)(ch))
#define TFW_H5_TIM_CR1_CEN     (0x1u << 0)
#define TFW_H5_TIM_CR1_ARPE    (0x1u << 7)
#define TFW_H5_TIM_EGR_UG      (0x1u << 0)
#define TFW_H5_PWM_UNIT        5u             /* the PWM row's unit: TIM5 */
#define TFW_H5_PWM_AF          2u             /* GPIO_AF2_TIM5 (stm32h5xx_hal_gpio_ex.h) */
#define TFW_H5_GPIO_AFRL(b)    TFW_CM_REG((b) + 0x20u)   /* AFR[0]: pins 0-7 */

/* ---- IWDG, APB1PERIPH_BASE_NS + 0x3000 */
#define TFW_H5_IWDG_KR         TFW_CM_REG(0x40003000u)
#define TFW_H5_IWDG_PR         TFW_CM_REG(0x40003004u)
#define TFW_H5_IWDG_RLR        TFW_CM_REG(0x40003008u)
#define TFW_H5_IWDG_SR         TFW_CM_REG(0x4000300Cu)

/* The key register's values (ST's stm32h5xx_ll_iwdg.h). */
#define TFW_H5_IWDG_KEY_RELOAD 0x0000AAAAu
#define TFW_H5_IWDG_KEY_ENABLE 0x0000CCCCu
#define TFW_H5_IWDG_KEY_ACCESS 0x00005555u

/* The status register's update-in-progress bits, PVU to EWU: a prescaler or
 * reload written while one is set is not yet the watchdog's. */
#define TFW_H5_IWDG_SR_BUSY    0x0Fu

#endif /* TFW_CM_STM32H5_REGS_H */
