/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link on UART0, a CMSDK APB UART: interrupt-driven, with the two
 * single-producer/single-consumer rings src/hal/avr/hal_uart.c has and for
 * its reasons. A byte load or store is one instruction on a Cortex-M too, so
 * uint8_t indices keep each ring lock-free.
 *
 * The one difference is how a transmission starts. This UART has a one-byte
 * buffer and raises its transmit interrupt when a byte LEAVES it, not while
 * it is empty, so enabling the interrupt over an idle UART raises nothing.
 * tfw_hal_uart_tx_try therefore writes the first byte itself, and the
 * interrupt each byte raises on leaving sends the next; tx_busy says a byte
 * is in flight and an interrupt will follow. That start is the only critical
 * section here: it and the interrupt both read tx_busy and the ring.
 *
 * The UART has two interrupt lines, one per direction, wired to the external
 * interrupts the board profile names (32 and 33 on the AN505, 0 and 1 on the
 * AN500). Both run the same service routine, which reads the UART's own
 * status to see which direction is due, so the routine does not depend on
 * which line is which.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "soc_regs.h"

/* The ring sizes are board data (board_profile.h). The receive ring must hold
 * a whole frame at the longest payload: QEMU's UART paces nothing, so a request
 * arrives as fast as this ISR reads DATA, and a ring that fills drops the tail
 * of the frame -- the framer then waits out TFW_FRAME_IDLE_MS and the host
 * times out. The ring holds one byte less than its size. */
_Static_assert(TFW_UART_RX_RING - 1u >= TFW_MAX_FRAME,
               "TFW_UART_RX_RING must hold a whole frame at TFW_MAX_PAYLOAD");
_Static_assert((TFW_UART_RX_RING & (TFW_UART_RX_RING - 1u)) == 0u,
               "TFW_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert((TFW_UART_TX_RING & (TFW_UART_TX_RING - 1u)) == 0u,
               "TFW_UART_TX_RING must be a power of two: the index wraps by masking");
_Static_assert(TFW_UART_RX_RING <= 256u && TFW_UART_TX_RING <= 256u,
               "ring indices must fit in a uint8_t");

void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_UART0_IRQ_A)(void);
void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_UART0_IRQ_B)(void);

static volatile uint8_t rx_buf[TFW_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */
static volatile uint8_t rx_dropped;         /* bytes the full ring refused; saturates. Read by
                                               the QEMU row's failure dump (qmp_regs.py). */

static volatile uint8_t tx_buf[TFW_UART_TX_RING];
static volatile uint8_t tx_head, tx_tail;   /* head: main writes. tail: ISR and the start. */
static volatile uint8_t tx_busy;            /* a byte is in the UART; its interrupt will follow */

tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    uint32_t div;

    if (baud == 0u) {
        return TFW_ERR_RANGE;
    }
    div = (uint32_t)(TFW_CM_CPU_HZ / baud);
    if (div < TFW_MPS2_UART_BAUDDIV_MIN) {
        return TFW_ERR_RANGE;
    }
    TFW_MPS2_UART_CTRL    = 0u;
    TFW_MPS2_UART_BAUDDIV = div;
    TFW_MPS2_UART_STATE   = TFW_MPS2_UART_STATE_TXOVR | TFW_MPS2_UART_STATE_RXOVR;
    TFW_MPS2_UART_INT     = TFW_MPS2_UART_INT_TX | TFW_MPS2_UART_INT_RX;
    rx_head = rx_tail = tx_head = tx_tail = 0u;
    tx_busy = 0u;
    TFW_MPS2_UART_CTRL = TFW_MPS2_UART_CTRL_TXEN | TFW_MPS2_UART_CTRL_RXEN |
                         TFW_MPS2_UART_CTRL_TXINT | TFW_MPS2_UART_CTRL_RXINT;
    tfw_cm_nvic_enable((uint32_t)TFW_MPS2_UART0_IRQ_A);
    tfw_cm_nvic_enable((uint32_t)TFW_MPS2_UART0_IRQ_B);
    return TFW_OK;
}

static void uart0_service(void)
{
    uint32_t pending = TFW_MPS2_UART_INT;

    TFW_MPS2_UART_INT = pending & (TFW_MPS2_UART_INT_TX | TFW_MPS2_UART_INT_RX);

    while ((TFW_MPS2_UART_STATE & TFW_MPS2_UART_STATE_RXFULL) != 0u) {
        uint8_t d    = (uint8_t)TFW_MPS2_UART_DATA;
        uint8_t next = (uint8_t)((rx_head + 1u) & (TFW_UART_RX_RING - 1u));

        /* A full ring drops the byte, as on AVR: the framer resynchronises on
         * a frame that fails its check, which beats a frame silently mended
         * with the wrong bytes. */
        if (next != rx_tail) {
            rx_buf[rx_head] = d;
            rx_head         = next;
        } else if (rx_dropped != 0xFFu) {
            rx_dropped++;
        }
    }
    TFW_MPS2_UART_STATE = TFW_MPS2_UART_STATE_RXOVR;

    if ((pending & TFW_MPS2_UART_INT_TX) != 0u) {
        if (tx_tail == tx_head) {
            tx_busy = 0u;
        } else {
            TFW_MPS2_UART_DATA = tx_buf[tx_tail];
            tx_tail = (uint8_t)((tx_tail + 1u) & (TFW_UART_TX_RING - 1u));
        }
    }
}

void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_UART0_IRQ_A)(void)
{
    uart0_service();
}

void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_UART0_IRQ_B)(void)
{
    uart0_service();
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
    if (!tx_busy) {
        tx_busy            = 1u;
        TFW_MPS2_UART_DATA = tx_buf[tx_tail];
        tx_tail = (uint8_t)((tx_tail + 1u) & (TFW_UART_TX_RING - 1u));
    }
    tfw_cm_irq_restore(primask);
    return 1u;
}

/* An empty ring and no byte in the UART: the deferred reset waits on this so
 * the acknowledgement's last byte is out before the reset. */
uint8_t tfw_hal_uart_tx_idle(void)
{
    return (uint8_t)(tx_head == tx_tail && !tx_busy &&
                     (TFW_MPS2_UART_STATE & TFW_MPS2_UART_STATE_TXFULL) == 0u);
}
