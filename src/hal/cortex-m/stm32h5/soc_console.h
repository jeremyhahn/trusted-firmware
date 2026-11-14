/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The console USART, the byte link on the board's ST-LINK Virtual COM port,
 * as board data: which USART, its clock enable, its interrupt and its pins.
 * hal_uart.c reads only these TFW_H5_CON_* names. Include after tfw/config.h,
 * which brings the board's board_profile.h, and after soc_regs.h, whose names
 * the STM32H573I-DK's values and the register macros expand to. It includes
 * neither, so a board's host test reads the USART3 values with no part
 * headers (test/unit/test_board_nucleo-h563zi.c).
 *
 *   TFW_H5_CONSOLE_USART3 set   the NUCLEO-H563ZI: USART3 on PD8 TX, PD9 RX
 *   otherwise                   the STM32H573I-DK: USART1 on PA9 TX, PA10 RX,
 *                               soc_regs.h's values unchanged
 *
 * Both USARTs count the same kernel clock at reset, PCLK1 or PCLK2 at the
 * undivided 64 MHz HSI (hal_soc.c), so one divisor serves both.
 */
#ifndef TFW_H5_SOC_CONSOLE_H
#define TFW_H5_SOC_CONSOLE_H

#if defined(TFW_H5_CONSOLE_USART3) && TFW_H5_CONSOLE_USART3

/* USART3 on the NUCLEO-H563ZI (MB1404), the ST-LINK/V3E Virtual COM port.
 * TO CONFIRM against ST's stm32h563xx.h and UM3115, neither held here
 * (docs/boards/nucleo-h563zi.md): the base APB1PERIPH_BASE_NS + 0x4800,
 * RCC_APB1LENR at RCC + 0x9C with USART3EN bit 18, USART3_IRQn 60, and the
 * pins PD8 TX / PD9 RX on alternate function 7. */
#define TFW_H5_CON_BASE        0x40004800u
#define TFW_H5_CON_ENR_OFF     0x9Cu          /* RCC_APB1LENR */
#define TFW_H5_CON_EN          (0x1u << 18)   /* USART3EN */
#define TFW_H5_CON_IRQ         60u            /* USART3_IRQn */
#define TFW_H5_CON_HANDLER     tfw_cm_irq60
#define TFW_H5_CON_BANK        3u             /* GPIOD */
#define TFW_H5_CON_TX_PIN      8u
#define TFW_H5_CON_RX_PIN      9u
#define TFW_H5_CON_AF          7u

#else

/* USART1 on the STM32H573I-DK, soc_regs.h's values. */
#define TFW_H5_CON_BASE        TFW_H5_USART1_BASE
#define TFW_H5_CON_ENR_OFF     0xA4u          /* RCC_APB2ENR */
#define TFW_H5_CON_EN          TFW_H5_RCC_APB2ENR_USART1EN
#define TFW_H5_CON_IRQ         TFW_H5_USART1_IRQ
#define TFW_H5_CON_HANDLER     tfw_cm_irq58
#define TFW_H5_CON_BANK        TFW_H5_VCP_BANK
#define TFW_H5_CON_TX_PIN      TFW_H5_VCP_TX_PIN
#define TFW_H5_CON_RX_PIN      TFW_H5_VCP_RX_PIN
#define TFW_H5_CON_AF          TFW_H5_VCP_AF

#endif

#define TFW_H5_CON_ENR         TFW_CM_REG(TFW_H5_RCC_BASE + TFW_H5_CON_ENR_OFF)

/* The six registers hal_uart.c drives, at the console's base; the bits are
 * soc_regs.h's TFW_H5_USART_*, the same on every USART. */
#define TFW_H5_CON_CR1         TFW_CM_REG(TFW_H5_CON_BASE + 0x00u)
#define TFW_H5_CON_BRR         TFW_CM_REG(TFW_H5_CON_BASE + 0x0Cu)
#define TFW_H5_CON_ISR         TFW_CM_REG(TFW_H5_CON_BASE + 0x1Cu)
#define TFW_H5_CON_ICR         TFW_CM_REG(TFW_H5_CON_BASE + 0x20u)
#define TFW_H5_CON_RDR         TFW_CM_REG(TFW_H5_CON_BASE + 0x24u)
#define TFW_H5_CON_TDR         TFW_CM_REG(TFW_H5_CON_BASE + 0x28u)

#endif /* TFW_H5_SOC_CONSOLE_H */
