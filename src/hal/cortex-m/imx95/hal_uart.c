/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link on LPUART3 (soc_regs.h; TO CONFIRM against NXP IMX95RM, the
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
#include "hal_early.h"

#define TFW_UART_RX_RING 64u
#define TFW_UART_TX_RING 64u

_Static_assert((TFW_UART_RX_RING & (TFW_UART_RX_RING - 1u)) == 0u,
               "TFW_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert((TFW_UART_TX_RING & (TFW_UART_TX_RING - 1u)) == 0u,
               "TFW_UART_TX_RING must be a power of two: the index wraps by masking");
_Static_assert(TFW_UART_RX_RING <= 256u && TFW_UART_TX_RING <= 256u,
               "ring indices must fit in a uint8_t");

void tfw_cm_irq64(void);

static volatile uint8_t rx_buf[TFW_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */

static volatile uint8_t tx_buf[TFW_UART_TX_RING];
static volatile uint8_t tx_head, tx_tail;   /* head: main writes. tail: tx_pump, masked. */

/* Times LPUART3's interrupt ran, for the bench: the A55 reads it through the
 * DTCM alias (docs/development/bench/frdm-imx95.md, next slice), its address
 * from the image's symbols. Zero beside a sent frame means NVIC line 64 never
 * fired and the main loop's polls below carried the console. */
__attribute__((used)) volatile uint32_t tfw_imx95_uart_irqs;

/* Moves the ring to DATA while TDRE is set and keeps TIE up only while bytes
 * remain. Called by the interrupt and, masked, by every entry the main loop
 * polls: the console does not hang on the interrupt alone, so a line that
 * never fires slows it instead of silencing it. */
static void tx_pump(void)
{
    while (tx_tail != tx_head &&
           (TFW_IMX95_LPUART_STAT & TFW_IMX95_LPUART_STAT_TDRE) != 0u) {
        TFW_IMX95_LPUART_DATA = tx_buf[tx_tail];
        tx_tail = (uint8_t)((tx_tail + 1u) & (TFW_UART_TX_RING - 1u));
    }
    if (tx_tail != tx_head) {
        TFW_IMX95_LPUART_CTRL |= TFW_IMX95_LPUART_CTRL_TIE;
    } else if ((TFW_IMX95_LPUART_CTRL & TFW_IMX95_LPUART_CTRL_TIE) != 0u) {
        TFW_IMX95_LPUART_CTRL &= ~TFW_IMX95_LPUART_CTRL_TIE;
    }
}

tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    uint32_t     reg;
    tfw_status_t st = tfw_imx95_lpuart_baud((uint32_t)TFW_IMX95_LPUART_CLK_HZ, baud, &reg);

    if (st != TFW_OK) {
        return st;
    }
    /* The pads first: the SM owns the IOMUX and routes LPUART3 to them only
     * when asked. A refusal leaves the console to start anyway, as the SM
     * may have routed them already (console_pins.h). */
    (void)tfw_imx95_console_route();
    TFW_CM_STEP('P');
    /* A reset first: BAUD is written only with the transmitter and receiver
     * off, and whatever ran before (the ROM, a previous image) left them on. */
    TFW_IMX95_LPUART_GLOBAL = TFW_IMX95_LPUART_GLOBAL_RST;
    TFW_IMX95_LPUART_GLOBAL = 0u;
    TFW_IMX95_LPUART_BAUD   = reg;
    TFW_IMX95_LPUART_STAT   = TFW_IMX95_LPUART_STAT_OR;
    rx_head = rx_tail = tx_head = tx_tail = 0u;
    TFW_IMX95_LPUART_CTRL = TFW_IMX95_LPUART_CTRL_TE | TFW_IMX95_LPUART_CTRL_RE |
                            TFW_IMX95_LPUART_CTRL_RIE;
    tfw_cm_nvic_enable(TFW_IMX95_LPUART3_IRQ);
    TFW_CM_STEP('U');
    return TFW_OK;
}

void tfw_cm_irq64(void)
{
    tfw_imx95_uart_irqs++;
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

    tx_pump();
}

uint8_t tfw_hal_uart_rx(uint8_t *out)
{
    uint32_t primask = tfw_cm_irq_save();

    /* The main loop polls here, so it moves the transmitter on too. */
    tx_pump();
    tfw_cm_irq_restore(primask);
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
    tx_pump();
    tfw_cm_irq_restore(primask);
    return 1u;
}

/* An empty ring and the last stop bit out (STAT.TC): the deferred reset waits
 * on this so the acknowledgement's last byte is out before the reset. */
uint8_t tfw_hal_uart_tx_idle(void)
{
    uint32_t primask = tfw_cm_irq_save();

    tx_pump();                          /* a caller waiting here drains the ring */
    tfw_cm_irq_restore(primask);
    return (uint8_t)(tx_head == tx_tail &&
                     (TFW_IMX95_LPUART_STAT & TFW_IMX95_LPUART_STAT_TC) != 0u);
}
