/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's UART on the STM32H5: USART1 on PA9/PA10, the USART the
 * application drives (src/hal/cortex-m/stm32h5/hal_uart.c), polled.
 * boot/boot_hal.h says why the bootloader polls; here it also means tfw-boot
 * enables no interrupt at all, so its vector table holds no live entry the
 * application could inherit.
 *
 * tfw_cm_boot_uart_off puts back what init changed, to the reset values
 * (ST's stm32h5xx_hal_rcc.c: every peripheral clock off, every pin analog),
 * so the application starts from the state a bare reset gives it.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "../../../boot_hal.h"
#include "../boot_soc.h"
#include "stm32h5/soc_regs.h"
#include "stm32h5/h5_calc.h"

void tfw_boot_uart_init(uint32_t baud)
{
    /* The divisor the application computes. A rate this clock cannot divide
     * to is a build-time constant (TFW_UART_BAUD) and the application refuses
     * it at init; here the USART is left off, so the session is silent rather
     * than garbled. */
    uint32_t brr  = tfw_h5_usart_brr((uint32_t)TFW_CM_CPU_HZ, baud);
    uint32_t bank = tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, TFW_H5_VCP_BANK);

    if (brr == 0u) {
        return;
    }
    TFW_H5_RCC_AHB2ENR |= 1u << TFW_H5_VCP_BANK;
    TFW_H5_RCC_APB2ENR |= TFW_H5_RCC_APB2ENR_USART1EN;
    (void)TFW_H5_RCC_APB2ENR;                   /* both enables land before either block is written */
    TFW_H5_GPIO_AFRH(bank) = tfw_h5_field4(tfw_h5_field4(TFW_H5_GPIO_AFRH(bank),
                                                         TFW_H5_VCP_TX_PIN, TFW_H5_VCP_AF),
                                           TFW_H5_VCP_RX_PIN, TFW_H5_VCP_AF);
    TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(tfw_h5_field2(TFW_H5_GPIO_MODER(bank),
                                                          TFW_H5_VCP_TX_PIN, TFW_H5_MODE_AF),
                                            TFW_H5_VCP_RX_PIN, TFW_H5_MODE_AF);
    TFW_H5_USART_CR1 = 0u;
    TFW_H5_USART_BRR = brr;
    TFW_H5_USART_CR1 = TFW_H5_USART_CR1_UE | TFW_H5_USART_CR1_RE | TFW_H5_USART_CR1_TE;
}

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    uint32_t isr = TFW_H5_USART_ISR;

    if ((isr & TFW_H5_USART_ISR_ORE) != 0u) {
        TFW_H5_USART_ICR = TFW_H5_USART_ICR_ORECF;   /* an overrun stops reception until cleared */
    }
    if ((isr & TFW_H5_USART_ISR_RXNE) == 0u) {
        return 0u;
    }
    *out = (uint8_t)TFW_H5_USART_RDR;
    return 1u;
}

void tfw_boot_uart_tx(uint8_t byte)
{
    if ((TFW_H5_USART_CR1 & TFW_H5_USART_CR1_TE) == 0u) {
        return;                         /* never enabled: TXE would never set */
    }
    while ((TFW_H5_USART_ISR & TFW_H5_USART_ISR_TXE) == 0u) { }
    TFW_H5_USART_TDR = byte;
}

void tfw_cm_boot_uart_off(void)
{
    uint32_t bank = tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, TFW_H5_VCP_BANK);

    if ((TFW_H5_USART_CR1 & TFW_H5_USART_CR1_TE) == 0u) {
        return;                         /* init left it off: nothing was changed */
    }
    while ((TFW_H5_USART_ISR & TFW_H5_USART_ISR_TC) == 0u) { }
    TFW_H5_USART_CR1 = 0u;
    TFW_H5_USART_BRR = 0u;
    TFW_H5_USART_ICR = TFW_H5_USART_ICR_ORECF;
    TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(tfw_h5_field2(TFW_H5_GPIO_MODER(bank),
                                                          TFW_H5_VCP_TX_PIN, TFW_H5_MODE_ANALOG),
                                            TFW_H5_VCP_RX_PIN, TFW_H5_MODE_ANALOG);
    TFW_H5_GPIO_AFRH(bank) = tfw_h5_field4(tfw_h5_field4(TFW_H5_GPIO_AFRH(bank),
                                                         TFW_H5_VCP_TX_PIN, 0u),
                                           TFW_H5_VCP_RX_PIN, 0u);
    TFW_H5_RCC_APB2ENR &= ~TFW_H5_RCC_APB2ENR_USART1EN;
    TFW_H5_RCC_AHB2ENR &= ~(1u << TFW_H5_VCP_BANK);
}
