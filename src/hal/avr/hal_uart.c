/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "ag/hal.h"
#include "ag/config.h"

/* The USART register names come from avr_usart_regs.h, which the bootloader
 * includes too -- see that file for why they are pasted from AG_UART_N and why
 * the interrupt vector names are not. */
#include "avr_usart_regs.h"

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

    AG_UBRRH = (uint8_t)(ubrr >> 8);
    AG_UBRRL = (uint8_t)(ubrr & 0xFFu);
    AG_UCSRA = (uint8_t)_BV(AG_U2X);
    AG_UCSRC = (uint8_t)(_BV(AG_UCSZ1) | _BV(AG_UCSZ0));          /* 8N1 */
    AG_UCSRB = (uint8_t)(_BV(AG_RXEN) | _BV(AG_TXEN) | _BV(AG_RXCIE));
    rx_head = rx_tail = tx_head = tx_tail = 0u;
    return AG_OK;
}

ISR(AG_UART_RX_VECT)
{
    uint8_t d    = AG_UDR;
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

ISR(AG_UART_UDRE_VECT)
{
    if (tx_tail == tx_head) {
        AG_UCSRB = (uint8_t)(AG_UCSRB & (uint8_t)~_BV(AG_UDRIE));  /* nothing left */
        return;
    }
    AG_UDR    = tx_buf[tx_tail];
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
    AG_UCSRB          = (uint8_t)(AG_UCSRB | _BV(AG_UDRIE));
    return 1u;
}

/* Empty ring AND an empty shift register. The deferred reset waits on this, and
 * the distinction matters: with the ring drained but the last byte still
 * shifting out, a reset truncates the final character of the acknowledgement
 * the host is waiting for. */
uint8_t ag_hal_uart_tx_idle(void)
{
    return (uint8_t)(tx_head == tx_tail && (AG_UCSRA & _BV(AG_TXC)) != 0u);
}
