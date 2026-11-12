/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link on the board's console USART, its ST-LINK Virtual COM port
 * (soc_console.h: USART1 on PA9/PA10 on the STM32H573I-DK, USART3 on PD8/PD9
 * on the NUCLEO-H563ZI): interrupt-driven, with the two single-producer/
 * single-consumer rings src/hal/avr/hal_uart.c has and for its reasons, and
 * uint8_t indices as on mps2-tz.
 *
 * The USART runs with its FIFO off, so its transmit interrupt is a level:
 * raised for as long as TDR is empty and TXEIE is set. That is AVR's UDRE,
 * not mps2-tz's edge, and the start follows AVR: tfw_hal_uart_tx_try queues
 * the byte and sets TXEIE, and the interrupt sends from the ring until it is
 * empty, then clears TXEIE. CR1 is the one register both sides change, so
 * the main side's read-modify-write of it is the one critical section.
 *
 * An overrun raises the receive interrupt too, and stays raised until ORECF
 * clears it; the service routine clears it, or it would run forever. The
 * byte it lost is lost, as a full ring's is: the framer resynchronises.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_console.h"
#include "h5_calc.h"

#define TFW_UART_RX_RING 64u
#define TFW_UART_TX_RING 64u

_Static_assert((TFW_UART_RX_RING & (TFW_UART_RX_RING - 1u)) == 0u,
               "TFW_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert((TFW_UART_TX_RING & (TFW_UART_TX_RING - 1u)) == 0u,
               "TFW_UART_TX_RING must be a power of two: the index wraps by masking");
_Static_assert(TFW_UART_RX_RING <= 256u && TFW_UART_TX_RING <= 256u,
               "ring indices must fit in a uint8_t");
_Static_assert(TFW_H5_CON_TX_PIN >= 8u && TFW_H5_CON_TX_PIN < TFW_H5_GPIO_PINS &&
               TFW_H5_CON_RX_PIN >= 8u && TFW_H5_CON_RX_PIN < TFW_H5_GPIO_PINS,
               "the console pins must be 8 to 15: vcp_pins writes AFRH only");

void TFW_H5_CON_HANDLER(void);

static volatile uint8_t rx_buf[TFW_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */

static volatile uint8_t tx_buf[TFW_UART_TX_RING];
static volatile uint8_t tx_head, tx_tail;   /* head: main writes. tail: ISR writes. */

/* The console's TX and RX pins to their alternate function, the bank's clock
 * first. Both pins are 8 to 15, so AFRH holds them. */
static void vcp_pins(void)
{
    uint32_t bank = tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, TFW_H5_CON_BANK);

    TFW_H5_RCC_AHB2ENR |= 1u << TFW_H5_CON_BANK;
    (void)TFW_H5_RCC_AHB2ENR;                   /* the enable lands before the bank is written */
    TFW_H5_GPIO_AFRH(bank) = tfw_h5_field4(tfw_h5_field4(TFW_H5_GPIO_AFRH(bank),
                                                         TFW_H5_CON_TX_PIN, TFW_H5_CON_AF),
                                           TFW_H5_CON_RX_PIN, TFW_H5_CON_AF);
    TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(tfw_h5_field2(TFW_H5_GPIO_MODER(bank),
                                                          TFW_H5_CON_TX_PIN, TFW_H5_MODE_AF),
                                            TFW_H5_CON_RX_PIN, TFW_H5_MODE_AF);
}

tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    uint32_t brr = tfw_h5_usart_brr((uint32_t)TFW_CM_CPU_HZ, baud);

    if (brr == 0u) {
        return TFW_ERR_RANGE;
    }
    vcp_pins();
    TFW_H5_CON_ENR |= TFW_H5_CON_EN;
    (void)TFW_H5_CON_ENR;
    TFW_H5_CON_CR1 = 0u;
    TFW_H5_CON_BRR = brr;
    TFW_H5_CON_ICR = TFW_H5_USART_ICR_ORECF;
    rx_head = rx_tail = tx_head = tx_tail = 0u;
    TFW_H5_CON_CR1 = TFW_H5_USART_CR1_UE | TFW_H5_USART_CR1_RE |
                       TFW_H5_USART_CR1_TE | TFW_H5_USART_CR1_RXNEIE;
    tfw_cm_nvic_enable(TFW_H5_CON_IRQ);
    return TFW_OK;
}

void TFW_H5_CON_HANDLER(void)
{
    uint32_t isr = TFW_H5_CON_ISR;

    if ((isr & TFW_H5_USART_ISR_RXNE) != 0u) {
        uint8_t d    = (uint8_t)TFW_H5_CON_RDR;
        uint8_t next = (uint8_t)((rx_head + 1u) & (TFW_UART_RX_RING - 1u));

        /* A full ring drops the byte, as on AVR: the framer resynchronises on
         * a frame that fails its check, which beats a frame silently mended
         * with the wrong bytes. */
        if (next != rx_tail) {
            rx_buf[rx_head] = d;
            rx_head         = next;
        }
    }
    if ((isr & TFW_H5_USART_ISR_ORE) != 0u) {
        TFW_H5_CON_ICR = TFW_H5_USART_ICR_ORECF;
    }

    if ((isr & TFW_H5_USART_ISR_TXE) != 0u &&
        (TFW_H5_CON_CR1 & TFW_H5_USART_CR1_TXEIE) != 0u) {
        if (tx_tail == tx_head) {
            TFW_H5_CON_CR1 &= ~TFW_H5_USART_CR1_TXEIE;
        } else {
            TFW_H5_CON_TDR = tx_buf[tx_tail];
            tx_tail = (uint8_t)((tx_tail + 1u) & (TFW_UART_TX_RING - 1u));
        }
    }
}

uint8_t tfw_hal_uart_rx(uint8_t *out)
{
    if (rx_tail == rx_head) {
        return 0u;
    }
    *out    = rx_buf[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1u) & (TFW_UART_RX_RING - 1u));
    return 1u;
}

uint8_t tfw_hal_uart_tx_try(uint8_t byte)
{
    uint8_t  next = (uint8_t)((tx_head + 1u) & (TFW_UART_TX_RING - 1u));
    uint32_t primask;

    if (next == tx_tail) {
        return 0u;                      /* full: the caller retries next poll */
    }
    tx_buf[tx_head] = byte;
    tx_head         = next;

    primask = tfw_cm_irq_save();
    TFW_H5_CON_CR1 |= TFW_H5_USART_CR1_TXEIE;
    tfw_cm_irq_restore(primask);
    return 1u;
}

/* An empty ring and the last byte off the wire (TC): the deferred reset waits
 * on this so the acknowledgement's last byte is out before the reset. */
uint8_t tfw_hal_uart_tx_idle(void)
{
    return (uint8_t)(tx_head == tx_tail &&
                     (TFW_H5_CON_ISR & TFW_H5_USART_ISR_TC) != 0u);
}
