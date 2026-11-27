/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link on LPUART7, the M7 console (soc_regs.h; TO CONFIRM against NXP IMX95RM, the
 * LPUART chapter): interrupt-driven, with the two single-producer/single-
 * consumer rings src/hal/cortex-m/mps2/hal_uart.c has and for its reasons.
 *
 * Unlike the CMSDK UART, the LPUART raises its transmit interrupt while its
 * data register is EMPTY (STAT.TDRE with CTRL.TIE), so a transmission starts
 * by enabling the interrupt, not by writing the first byte: tfw_hal_uart_tx_try
 * queues the byte and sets TIE, and the routine sends while TDRE holds and
 * clears TIE when the ring is empty. Setting and clearing TIE are
 * read-modify-writes of CTRL from both sides, so the main side's is the
 * critical section.
 *
 * The divisor comes from soc_calc.c, which the host tests pin, at the clock
 * root the board profile names (TFW_IMX95_LPUART_CLK_HZ).
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "console_pins.h"
#include "scmi.h"

#define TFW_UART_RX_RING 64u
#define TFW_UART_TX_RING 64u

_Static_assert((TFW_UART_RX_RING & (TFW_UART_RX_RING - 1u)) == 0u,
               "TFW_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert((TFW_UART_TX_RING & (TFW_UART_TX_RING - 1u)) == 0u,
               "TFW_UART_TX_RING must be a power of two: the index wraps by masking");
_Static_assert(TFW_UART_RX_RING <= 256u && TFW_UART_TX_RING <= 256u,
               "ring indices must fit in a uint8_t");

void tfw_cm_irq68(void);

static volatile uint8_t rx_buf[TFW_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */

static volatile uint8_t tx_buf[TFW_UART_TX_RING];
static volatile uint8_t tx_head, tx_tail;   /* head: main writes. tail: ISR writes. */

/* 1 only after an init that started the console. With the console off, tx
 * drops each byte and reports idle so a caller's backpressure loop and the
 * deferred reset never wait on a UART that cannot drain; rx reads nothing.
 * A refused baud returns before touching the console, so a running console
 * stays up. */
static volatile uint8_t uart_up;

/* tfw_imx95_console_route's result as the HAL's status: the channel's own
 * failures (scmi.h TFW_SCMI_E_*) and the SM's refusals (SCMI statuses). */
static tfw_status_t route_status(int32_t rc)
{
    switch (rc) {
    case 0:
        return TFW_OK;
    case TFW_SCMI_E_TIMEOUT:
        return TFW_ERR_TIMEOUT;
    case TFW_SCMI_E_CRC:
        return TFW_ERR_CRC;
    case TFW_SCMI_DENIED:
    case TFW_SCMI_BUSY:
    case TFW_SCMI_IN_USE:
        return TFW_ERR_BUSY;
    default:
        return TFW_ERR_INTERNAL;
    }
}

tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    uint32_t     reg;
    tfw_status_t st;

    st = tfw_imx95_lpuart_baud((uint32_t)TFW_IMX95_LPUART_CLK_HZ, baud, &reg);
    if (st != TFW_OK) {
        return st;
    }
    uart_up = 0u;
    /* The pads first: the SM owns the IOMUX and routes LPUART7 to them only
     * when asked. The console starts only when both pads were granted; a
     * refusal or a failed exchange turns the LPUART's transmitter, receiver
     * and interrupt off (whatever a previous image left on) and the status
     * comes back, so a refused RX never leaves a TX-only console
     * (console_pins.h). */
    st = route_status(tfw_imx95_console_route());
    if (st != TFW_OK) {
        tfw_cm_nvic_disable(TFW_IMX95_LPUART7_IRQ);
        TFW_IMX95_LPUART_CTRL &= ~(TFW_IMX95_LPUART_CTRL_TE | TFW_IMX95_LPUART_CTRL_RE |
                                   TFW_IMX95_LPUART_CTRL_RIE | TFW_IMX95_LPUART_CTRL_TIE);
        return st;
    }
    /* A reset first: BAUD is written only with the transmitter and receiver
     * off, and whatever ran before (the ROM, a previous image) left them on. */
    TFW_IMX95_LPUART_GLOBAL = TFW_IMX95_LPUART_GLOBAL_RST;
    TFW_IMX95_LPUART_GLOBAL = 0u;
    TFW_IMX95_LPUART_BAUD   = reg;
    TFW_IMX95_LPUART_STAT   = TFW_IMX95_LPUART_STAT_OR;
    rx_head = rx_tail = tx_head = tx_tail = 0u;
    TFW_IMX95_LPUART_CTRL = TFW_IMX95_LPUART_CTRL_TE | TFW_IMX95_LPUART_CTRL_RE |
                            TFW_IMX95_LPUART_CTRL_RIE;
    tfw_cm_nvic_enable(TFW_IMX95_LPUART7_IRQ);
    uart_up = 1u;
    return TFW_OK;
}

void tfw_cm_irq68(void)
{
    while ((TFW_IMX95_LPUART_STAT & TFW_IMX95_LPUART_STAT_RDRF) != 0u) {
        uint8_t d    = (uint8_t)TFW_IMX95_LPUART_DATA;
        uint8_t next = (uint8_t)((rx_head + 1u) & (TFW_UART_RX_RING - 1u));

        /* A full ring drops the byte, as on AVR: the framer resynchronises on
         * a frame that fails its check. */
        if (next != rx_tail) {
            rx_buf[rx_head] = d;
            rx_head         = next;
        }
    }
    /* An overrun stops reception until it is cleared. */
    if ((TFW_IMX95_LPUART_STAT & TFW_IMX95_LPUART_STAT_OR) != 0u) {
        TFW_IMX95_LPUART_STAT = TFW_IMX95_LPUART_STAT_OR;
    }

    while ((TFW_IMX95_LPUART_CTRL & TFW_IMX95_LPUART_CTRL_TIE) != 0u &&
           (TFW_IMX95_LPUART_STAT & TFW_IMX95_LPUART_STAT_TDRE) != 0u) {
        if (tx_tail == tx_head) {
            TFW_IMX95_LPUART_CTRL &= ~TFW_IMX95_LPUART_CTRL_TIE;
        } else {
            TFW_IMX95_LPUART_DATA = tx_buf[tx_tail];
            tx_tail = (uint8_t)((tx_tail + 1u) & (TFW_UART_TX_RING - 1u));
        }
    }
}

uint8_t tfw_hal_uart_rx(uint8_t *out)
{
    if (uart_up == 0u || rx_tail == rx_head) {
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

    if (uart_up == 0u) {
        return 1u;                      /* no console: dropped, not "full" */
    }
    if (next == tx_tail) {
        return 0u;                      /* full: the caller retries next poll */
    }
    tx_buf[tx_head] = byte;
    tx_head         = next;

    primask = tfw_cm_irq_save();
    TFW_IMX95_LPUART_CTRL |= TFW_IMX95_LPUART_CTRL_TIE;
    tfw_cm_irq_restore(primask);
    return 1u;
}

/* An empty ring and the last stop bit out (STAT.TC): the deferred reset waits
 * on this so the acknowledgement's last byte is out before the reset. */
uint8_t tfw_hal_uart_tx_idle(void)
{
    if (uart_up == 0u) {
        return 1u;
    }
    return (uint8_t)(tx_head == tx_tail &&
                     (TFW_IMX95_LPUART_STAT & TFW_IMX95_LPUART_STAT_TC) != 0u);
}
