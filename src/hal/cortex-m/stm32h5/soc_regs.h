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

/* ---- TIM3, APB1PERIPH_BASE_NS + 0x0400: the tone's timer (hal_tone.c), a
 * 16-bit general-purpose timer (not in IS_TIM_32B_COUNTER_INSTANCE). Offsets
 * are TIM_TypeDef's (CR1 0x00, EGR 0x14, CCMR1 0x18, CCER 0x20, PSC 0x28,
 * ARR 0x2C, CCR2 0x38); its enable RCC_APB1LENR (0x9C) bit TIM3EN 1. It counts
 * the APB1 timer clock, TFW_CM_CPU_HZ with the bus prescaler at 1. Channel 2,
 * whose output-compare mode OC2M is CCMR1 bits 14:12 and 24 (TIM_CCMR1_OC2M,
 * 0x1007 << 12) and enable CC2E CCER bit 4. The two modes are the LL
 * driver's (stm32h5xx_ll_tim.h): LL_TIM_OCMODE_TOGGLE, OC1M_1 | OC1M_0 (0011,
 * OCxREF toggles on match), and LL_TIM_OCMODE_FORCED_INACTIVE, OC1M_2 (0100,
 * OCxREF low), each moved to channel 2's field. */
#ifndef TFW_H5_TONE_TIM_HZ
#define TFW_H5_TONE_TIM_HZ       TFW_CM_CPU_HZ      /* the host test models another clock */
#endif
#define TFW_H5_TONE_RCC_ENR      TFW_CM_REG(TFW_H5_RCC_BASE + 0x9Cu)   /* RCC_APB1LENR */
#define TFW_H5_TONE_RCC_EN       (0x1u << 1)                          /* TIM3EN */
#define TFW_H5_TONE_TIM_BASE     0x40000400u
#define TFW_H5_TONE_TIM_CR1      TFW_CM_REG(TFW_H5_TONE_TIM_BASE + 0x00u)
#define TFW_H5_TONE_TIM_EGR      TFW_CM_REG(TFW_H5_TONE_TIM_BASE + 0x14u)
#define TFW_H5_TONE_TIM_CCMR1    TFW_CM_REG(TFW_H5_TONE_TIM_BASE + 0x18u)
#define TFW_H5_TONE_TIM_CCER     TFW_CM_REG(TFW_H5_TONE_TIM_BASE + 0x20u)
#define TFW_H5_TONE_TIM_PSC      TFW_CM_REG(TFW_H5_TONE_TIM_BASE + 0x28u)
#define TFW_H5_TONE_TIM_ARR      TFW_CM_REG(TFW_H5_TONE_TIM_BASE + 0x2Cu)
#define TFW_H5_TONE_TIM_CCR2     TFW_CM_REG(TFW_H5_TONE_TIM_BASE + 0x38u)
#define TFW_H5_TONE_CR1_CEN      (0x1u << 0)        /* TIM_CR1_CEN */
#define TFW_H5_TONE_EGR_UG       (0x1u << 0)        /* TIM_EGR_UG */
#define TFW_H5_TONE_CCMR1_CH2    0x0100FF00u        /* CC2S, OC2FE, OC2PE, OC2M, OC2CE, OC2M[3] */
#define TFW_H5_TONE_OC2M_TOGGLE  (0x3u << 12)       /* LL_TIM_OCMODE_TOGGLE on channel 2 */
#define TFW_H5_TONE_OC2M_LOW     (0x4u << 12)       /* LL_TIM_OCMODE_FORCED_INACTIVE on channel 2 */
#define TFW_H5_TONE_CCER_CH2     (0xBu << 4)        /* CC2E, CC2P, CC2NP: on, active high */
#define TFW_H5_TONE_CCER_CC2E    (0x1u << 4)        /* TIM_CCER_CC2E */
/* The tone pin: the Arduino header's D3, PB5 (ST's STM32_open_pin_data board
 * file labels PB5 ARD_D3; its GPIO modes file gives PB5 TIM3_CH2 as
 * GPIO_AF2_TIM3, which stm32h5xx_hal_gpio_ex.h defines as 2). */
#define TFW_H5_TONE_BANK         1u                 /* GPIOB */
#define TFW_H5_TONE_PIN          5u
#define TFW_H5_TONE_AF           2u                 /* GPIO_AF2_TIM3 */
#define TFW_H5_GPIO_AFR(b, n)    TFW_CM_REG((b) + 0x20u + 4u * (uint32_t)(n))   /* AFR[n] */

#endif /* TFW_CM_STM32H5_REGS_H */
