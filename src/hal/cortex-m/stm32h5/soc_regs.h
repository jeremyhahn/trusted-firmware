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

/* The ADC's bus clock and its kernel clock (RCC_TypeDef CCIPR5 at 0xE8;
 * RCC_AHB2ENR_ADCEN, RCC_CCIPR5_ADCDACSEL). ADCDACSEL 0 is the AHB clock,
 * HCLK (stm32h5xx_ll_rcc.h LL_RCC_ADCDAC_CLKSOURCE_HCLK), so no PLL starts. */
#define TFW_H5_RCC_CCIPR5      TFW_CM_REG(TFW_H5_RCC_BASE + 0xE8u)
#define TFW_H5_RCC_AHB2ENR_ADCEN    (0x1u << 10)
#define TFW_H5_RCC_CCIPR5_ADCDACSEL (0x7u << 0)
#define TFW_H5_RCC_ADCDACSEL_HCLK   0x0u

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

/* ---- ADC1, AHB2PERIPH_BASE_NS + 0x8000 (ADC1_BASE_NS), and the ADC1/ADC2
 * common block at + 0x8300 (ADC12_COMMON_BASE_NS). The offsets are
 * ADC_TypeDef's and ADC_Common_TypeDef's (CCR at 0x300 + 0x08). */
#define TFW_H5_ADC1_BASE       0x42028000u
#define TFW_H5_ADC1_ISR        TFW_CM_REG(TFW_H5_ADC1_BASE + 0x00u)
#define TFW_H5_ADC1_CR         TFW_CM_REG(TFW_H5_ADC1_BASE + 0x08u)
#define TFW_H5_ADC1_CFGR       TFW_CM_REG(TFW_H5_ADC1_BASE + 0x0Cu)
#define TFW_H5_ADC1_SMPR1      TFW_CM_REG(TFW_H5_ADC1_BASE + 0x14u)
#define TFW_H5_ADC1_SMPR2      TFW_CM_REG(TFW_H5_ADC1_BASE + 0x18u)
#define TFW_H5_ADC1_SQR1       TFW_CM_REG(TFW_H5_ADC1_BASE + 0x30u)
#define TFW_H5_ADC1_DR         TFW_CM_REG(TFW_H5_ADC1_BASE + 0x40u)
#define TFW_H5_ADC1_DIFSEL     TFW_CM_REG(TFW_H5_ADC1_BASE + 0xB0u)
#define TFW_H5_ADC1_OR         TFW_CM_REG(TFW_H5_ADC1_BASE + 0xC8u)
#define TFW_H5_ADC12_CCR       TFW_CM_REG(0x42028308u)

/* ISR flags, cleared by writing 1 (ADC_ISR_*). */
#define TFW_H5_ADC_ISR_ADRDY   (0x1u << 0)
#define TFW_H5_ADC_ISR_EOC     (0x1u << 2)
#define TFW_H5_ADC_ISR_EOS     (0x1u << 3)
#define TFW_H5_ADC_ISR_OVR     (0x1u << 4)

/* CR (ADC_CR_*). */
#define TFW_H5_ADC_CR_ADEN     (0x1u << 0)
#define TFW_H5_ADC_CR_ADDIS    (0x1u << 1)
#define TFW_H5_ADC_CR_ADSTART  (0x1u << 2)
#define TFW_H5_ADC_CR_ADSTP    (0x1u << 4)
#define TFW_H5_ADC_CR_ADVREGEN (0x1u << 28)
#define TFW_H5_ADC_CR_DEEPPWD  (0x1u << 29)
#define TFW_H5_ADC_CR_ADCALDIF (0x1u << 30)
#define TFW_H5_ADC_CR_ADCAL    (0x1u << 31)

/* CFGR (ADC_CFGR_*): RES 0 is 12 bits, ALIGN 0 right, CONT 0 single. */
#define TFW_H5_ADC_CFGR_RES    (0x3u << 3)
#define TFW_H5_ADC_CFGR_OVRMOD (0x1u << 12)
#define TFW_H5_ADC_CFGR_CONT   (0x1u << 13)
#define TFW_H5_ADC_CFGR_ALIGN  (0x1u << 15)

/* SMPR1 holds channels 0-9 and SMPR2 10-19, three bits each
 * (ADC_SMPR1_SMP0 to ADC_SMPR2_SMP19). Code 7 is 640.5 ADC clock cycles,
 * the longest (stm32h5xx_ll_adc.h LL_ADC_SAMPLINGTIME_640CYCLES_5). */
#define TFW_H5_ADC_SMP_640C5   0x7u

/* SQR1 (ADC_SQR1_L, ADC_SQR1_SQ1): L 0 is one conversion. */
#define TFW_H5_ADC_SQR1_L      (0xFu << 0)
#define TFW_H5_ADC_SQR1_SQ1_POS 6u
#define TFW_H5_ADC_SQR1_SQ1    (0x1Fu << 6)
#define TFW_H5_ADC_DR_12BIT    0x0FFFu
#define TFW_H5_ADC_CHANNELS    20u             /* INP0 to INP19, DIFSEL's width */

/* OR OP0 connects INP0 to its pin, PA0: stm32h5xx_ll_adc.h
 * LL_ADC_EnableChannel0_GPIO, "must be enabled when INP0 is used". */
#define TFW_H5_ADC_OR_OP0      (0x1u << 0)

/* CCR (ADC_CCR_CKMODE, ADC_CCR_PRESC): CKMODE 0 is the RCC kernel clock
 * through PRESC; PRESC 1 divides it by 2 (LL_ADC_CLOCK_ASYNC_DIV2). */
#define TFW_H5_ADC_CCR_CKMODE  (0x3u << 16)
#define TFW_H5_ADC_CCR_PRESC_POS 18u
#define TFW_H5_ADC_CCR_PRESC   (0xFu << 18)

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
