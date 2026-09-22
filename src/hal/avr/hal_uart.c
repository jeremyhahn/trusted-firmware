/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "ag/hal.h"
#include "ag/config.h"

/*
 * Interrupt-driven USART with two single-producer/single-consumer rings.
 *
 * NO LOCKS, and none are needed. Each ring has exactly one writer and one
 * reader, and they are in different contexts: the ISR fills RX and drains TX,
 * main() drains RX and fills TX. With power-of-two sizes and uint8_t indices,
 * each index is written by one side only and read by the other, and a byte-wide
 * load or store on AVR is a single instruction and therefore atomic.
 *
 * That last clause is the load-bearing one. It is why the sizes must stay <= 256
 * and why the _Static_assert below is not decoration: at 512 the indices become
 * two bytes, the single-instruction property evaporates, and the reader can
 * observe a half-updated index -- a corruption that appears under load, looks
 * like line noise, and is very hard to attribute.
 */
#ifndef AG_UART_RX_RING
#define AG_UART_RX_RING 64u
#endif
#ifndef AG_UART_TX_RING
#define AG_UART_TX_RING 64u
#endif

_Static_assert((AG_UART_RX_RING & (AG_UART_RX_RING - 1u)) == 0u,
               "AG_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert((AG_UART_TX_RING & (AG_UART_TX_RING - 1u)) == 0u,
               "AG_UART_TX_RING must be a power of two: the index wraps by masking");
_Static_assert(AG_UART_RX_RING <= 256u && AG_UART_TX_RING <= 256u,
               "ring indices must fit in a uint8_t, or the lock-free single-writer "
               "property depends on a multi-byte store being atomic, which it is not");

static volatile uint8_t rx_buf[AG_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */

static volatile uint8_t tx_buf[AG_UART_TX_RING];
static volatile uint8_t tx_head, tx_tail;   /* head: main writes. tail: ISR writes. */

ag_status_t ag_hal_uart_init(uint32_t baud)
{
    /* Double-speed mode: at 16 MHz and 115200 the normal divisor has 3.5%
     * error, which is outside what a UART tolerates over a full frame. U2X
     * halves it to 2.1% and the link works. */
    uint16_t ubrr = (uint16_t)((F_CPU / (8UL * baud)) - 1UL);

    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFFu);
    UCSR0A = (uint8_t)_BV(U2X0);
    UCSR0C = (uint8_t)(_BV(UCSZ01) | _BV(UCSZ00));          /* 8N1 */
    UCSR0B = (uint8_t)(_BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0));
    rx_head = rx_tail = tx_head = tx_tail = 0u;
    return AG_OK;
}

ISR(USART_RX_vect)
{
    uint8_t d    = UDR0;
    uint8_t next = (uint8_t)((rx_head + 1u) & (AG_UART_RX_RING - 1u));

    /* A full ring drops the byte rather than overwriting the oldest. The framer
     * above will see a CRC failure or a bad length and resynchronise, which is
     * a recoverable frame loss; silently replacing bytes inside a frame would
     * produce a corrupt frame that still passes a length check. */
    if (next != rx_tail) {
        rx_buf[rx_head] = d;
        rx_head         = next;
    }
}

ISR(USART_UDRE_vect)
{
    if (tx_tail == tx_head) {
        UCSR0B = (uint8_t)(UCSR0B & (uint8_t)~_BV(UDRIE0));  /* nothing left */
        return;
    }
    UDR0    = tx_buf[tx_tail];
    tx_tail = (uint8_t)((tx_tail + 1u) & (AG_UART_TX_RING - 1u));
}

uint8_t ag_hal_uart_rx(uint8_t *out)
{
    if (rx_tail == rx_head) {
        return 0u;
    }
    *out    = rx_buf[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1u) & (AG_UART_RX_RING - 1u));
    return 1u;
}

uint8_t ag_hal_uart_tx_try(uint8_t byte)
{
    uint8_t next = (uint8_t)((tx_head + 1u) & (AG_UART_TX_RING - 1u));

    if (next == tx_tail) {
        return 0u;                      /* full: the caller retries next poll */
    }
    tx_buf[tx_head] = byte;
    tx_head         = next;
    UCSR0B          = (uint8_t)(UCSR0B | _BV(UDRIE0));
    return 1u;
}

/* Empty ring AND an empty shift register. The deferred reset waits on this, and
 * the distinction matters: with the ring drained but the last byte still
 * shifting out, a reset truncates the final character of the acknowledgement
 * the host is waiting for. */
uint8_t ag_hal_uart_tx_idle(void)
{
    return (uint8_t)(tx_head == tx_tail && (UCSR0A & _BV(TXC0)) != 0u);
}
