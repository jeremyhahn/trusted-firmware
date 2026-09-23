/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "ag/hal.h"
#include "ag/config.h"

/*
 * WHICH USART, and why this is not a literal.
 *
 * Three parts, three answers. The 328P and the Mega talk over USART0; the
 * 32U4's TTL pins are USART1, because its USART0 is the USB peripheral and this
 * firmware does not implement CDC. So the register names differ by an index the
 * board profile supplies, and everything below is pasted from it.
 *
 * The interrupt VECTOR names are given explicitly by the profile rather than
 * pasted, because avr-libc is not consistent about them: the 328P spells it
 * USART_RX_vect with no index at all, the Mega spells the same interrupt
 * USART0_RX_vect. Pasting would silently produce a name that does not exist,
 * and a misspelled ISR is not a link error -- it compiles to an ordinary
 * function nothing calls, the vector stays at its default, and the symptom is a
 * device that never answers. avr-gcc warns about it, and -Werror turns that
 * warning into the build break it deserves to be.
 */
#define AG_CAT_(a, b)      a##b
#define AG_CAT(a, b)       AG_CAT_(a, b)
/* Three-way paste in one step. Nesting AG_CAT inside itself does NOT work:
 * ## suppresses expansion of its own operands, so the outer paste sees the
 * literal text "AG_CAT(UCSR, AG_UART_N)" and tries to glue ")" to "A". */
#define AG_CAT3_(a, b, c)  a##b##c
#define AG_CAT3(a, b, c)   AG_CAT3_(a, b, c)

#define AG_UDR    AG_CAT(UDR,  AG_UART_N)
#define AG_UCSRA  AG_CAT3(UCSR, AG_UART_N, A)
#define AG_UCSRB  AG_CAT3(UCSR, AG_UART_N, B)
#define AG_UCSRC  AG_CAT3(UCSR, AG_UART_N, C)
#define AG_UBRRH  AG_CAT3(UBRR, AG_UART_N, H)
#define AG_UBRRL  AG_CAT3(UBRR, AG_UART_N, L)
#define AG_U2X    AG_CAT(U2X,   AG_UART_N)
#define AG_UCSZ0  AG_CAT3(UCSZ, AG_UART_N, 0)
#define AG_UCSZ1  AG_CAT3(UCSZ, AG_UART_N, 1)
#define AG_RXEN   AG_CAT(RXEN,  AG_UART_N)
#define AG_TXEN   AG_CAT(TXEN,  AG_UART_N)
#define AG_RXCIE  AG_CAT(RXCIE, AG_UART_N)
#define AG_UDRIE  AG_CAT(UDRIE, AG_UART_N)
#define AG_TXC    AG_CAT(TXC,   AG_UART_N)

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
