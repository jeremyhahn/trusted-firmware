/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link on UARTE20, the DK's console: interrupt-driven, with the two
 * single-producer/single-consumer rings src/hal/cortex-m/mps2/hal_uart.c
 * has and for its reasons.
 *
 * A UARTE moves bytes only by EasyDMA, from and to RAM. Each direction here
 * moves one byte per transfer, so the rings stay this file's and the driver
 * keeps the shape of every other port's:
 *
 *   receive   a one-byte transfer into rx_dma. Its end event's interrupt
 *             moves the byte into the ring and starts the next. The UARTE
 *             holds bytes that arrive between the two in its own receive
 *             FIFO and delivers them to the next transfer.
 *   transmit  tfw_hal_uart_tx_try starts a one-byte transfer from tx_dma.
 *             TXDRDY, raised when the byte has left the TXD line, sends the
 *             next or clears tx_busy. Waiting for TXDRDY rather than the
 *             transfer's end means tx_busy covers the byte on the wire, so
 *             tfw_hal_uart_tx_idle needs no status of the UARTE's own.
 *
 * The start of a transmission is the only critical section: it and the
 * interrupt both read tx_busy and the ring. A receive error (overrun,
 * framing, break) is cleared and the byte kept or dropped by the framer's
 * check, as on AVR.
 *
 * The baud rate comes from the vendor's table (soc_tables.c): UARTE20 is
 * clocked at 16 MHz whatever the CPU runs at.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_tables.h"

#define TFW_UART_RX_RING 64u
#define TFW_UART_TX_RING 64u

_Static_assert((TFW_UART_RX_RING & (TFW_UART_RX_RING - 1u)) == 0u,
               "TFW_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert((TFW_UART_TX_RING & (TFW_UART_TX_RING - 1u)) == 0u,
               "TFW_UART_TX_RING must be a power of two: the index wraps by masking");
_Static_assert(TFW_UART_RX_RING <= 256u && TFW_UART_TX_RING <= 256u,
               "ring indices must fit in a uint8_t");

void tfw_cm_irq198(void);

static volatile uint8_t rx_buf[TFW_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */

static volatile uint8_t tx_buf[TFW_UART_TX_RING];
static volatile uint8_t tx_head, tx_tail;   /* head: main writes. tail: ISR and the start. */
static volatile uint8_t tx_busy;            /* a byte is out; its TXDRDY will follow */

/* EasyDMA's two one-byte buffers, in RAM as it requires. */
static volatile uint8_t rx_dma;
static volatile uint8_t tx_dma;

static void rx_start(void)
{
    TFW_NRF_UARTE_RX_PTR    = (uint32_t)(uintptr_t)&rx_dma;
    TFW_NRF_UARTE_RX_MAXCNT = 1u;
    TFW_NRF_UARTE_RX_START  = 1u;
}

/* Caller holds tx_tail's ring non-empty and tx_busy set. */
static void tx_send_next(void)
{
    tx_dma  = tx_buf[tx_tail];
    tx_tail = (uint8_t)((tx_tail + 1u) & (TFW_UART_TX_RING - 1u));
    TFW_NRF_UARTE_TX_PTR    = (uint32_t)(uintptr_t)&tx_dma;
    TFW_NRF_UARTE_TX_MAXCNT = 1u;
    TFW_NRF_UARTE_TX_START  = 1u;
}

tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    uint32_t reg;

    if (tfw_nrf54l_uarte_baud(baud, &reg) != TFW_OK) {
        return TFW_ERR_RANGE;
    }
    tfw_cm_nvic_disable(TFW_NRF_UARTE_IRQ);
    TFW_NRF_UARTE_ENABLE   = 0u;
    TFW_NRF_UARTE_INTEN    = 0u;

    /* TX idles high: drive it so before the UARTE takes it. RX pulls up, as
     * the vendor's pin configuration does, so an unplugged probe reads idle
     * rather than a stream of breaks. */
    TFW_NRF_GPIO_OUTSET(TFW_NRF_P1_BASE) = 1uL << TFW_NRF_CONSOLE_TX_BIT;
    TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_CONSOLE_TX_BIT) = TFW_NRF_PIN_CNF_OUTPUT;
    TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_CONSOLE_RX_BIT) =
        TFW_NRF_PIN_CNF_INPUT | TFW_NRF_PIN_CNF_PULLUP;

    TFW_NRF_UARTE_PSEL_TXD = tfw_nrf54l_psel(TFW_NRF_CONSOLE_PORT, TFW_NRF_CONSOLE_TX_BIT);
    TFW_NRF_UARTE_PSEL_RXD = tfw_nrf54l_psel(TFW_NRF_CONSOLE_PORT, TFW_NRF_CONSOLE_RX_BIT);
    TFW_NRF_UARTE_PSEL_CTS = TFW_NRF54L_PSEL_DISCONNECTED;
    TFW_NRF_UARTE_PSEL_RTS = TFW_NRF54L_PSEL_DISCONNECTED;
    TFW_NRF_UARTE_BAUDRATE = reg;
    TFW_NRF_UARTE_CONFIG   = TFW_NRF_UARTE_CONFIG_8N1;

    TFW_NRF_UARTE_RX_END   = 0u;
    TFW_NRF_UARTE_TX_END   = 0u;
    TFW_NRF_UARTE_TXDRDY   = 0u;
    TFW_NRF_UARTE_ERROR    = 0u;
    TFW_NRF_UARTE_ERRORSRC = TFW_NRF_UARTE_ERRORSRC_ALL;
    rx_head = rx_tail = tx_head = tx_tail = 0u;
    tx_busy = 0u;

    TFW_NRF_UARTE_ENABLE = TFW_NRF_UARTE_ENABLE_ON;
    TFW_NRF_UARTE_INTEN  = TFW_NRF_UARTE_INTEN_RXEND | TFW_NRF_UARTE_INTEN_TXDRDY;
    rx_start();
    tfw_cm_nvic_enable(TFW_NRF_UARTE_IRQ);
    return TFW_OK;
}

void tfw_cm_irq198(void)
{
    if (TFW_NRF_UARTE_ERROR != 0u) {
        TFW_NRF_UARTE_ERROR    = 0u;
        TFW_NRF_UARTE_ERRORSRC = TFW_NRF_UARTE_ERRORSRC_ALL;
    }

    if (TFW_NRF_UARTE_RX_END != 0u) {
        uint8_t d    = rx_dma;
        uint8_t next = (uint8_t)((rx_head + 1u) & (TFW_UART_RX_RING - 1u));

        TFW_NRF_UARTE_RX_END = 0u;
        rx_start();
        /* A full ring drops the byte, as on AVR: the framer resynchronises on
         * a frame that fails its check, which beats a frame silently mended
         * with the wrong bytes. */
        if (next != rx_tail) {
            rx_buf[rx_head] = d;
            rx_head         = next;
        }
    }

    if (TFW_NRF_UARTE_TXDRDY != 0u) {
        TFW_NRF_UARTE_TXDRDY = 0u;
        TFW_NRF_UARTE_TX_END = 0u;
        if (tx_tail == tx_head) {
            tx_busy = 0u;
        } else {
            tx_send_next();
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
    if (!tx_busy) {
        tx_busy = 1u;
        tx_send_next();
    }
    tfw_cm_irq_restore(primask);
    return 1u;
}

/* An empty ring and no byte on the wire: the deferred reset waits on this so
 * the acknowledgement's last byte is out before the reset. */
uint8_t tfw_hal_uart_tx_idle(void)
{
    return (uint8_t)(tx_head == tx_tail && !tx_busy);
}
