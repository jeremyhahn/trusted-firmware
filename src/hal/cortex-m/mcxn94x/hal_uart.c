/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link on LP_FLEXCOMM4 as LPUART4, the FRDM-MCXN947's debug UART:
 * interrupt-driven, with the two single-producer/single-consumer rings
 * src/hal/avr/hal_uart.c has and for its reasons. A byte load or store is one
 * instruction on a Cortex-M too, so uint8_t indices keep each ring lock-free.
 *
 * The functional clock is FRO12M, selected and undivided here, and the BAUD
 * value is lpuart_baud.c's, which the host tests. 8N1, FIFOs off: a one-byte
 * buffer each way, as on the AVR.
 *
 * Transmit is level-triggered, unlike mps2-tz's UART: the transmit interrupt
 * is raised for as long as the data register is empty and TIE is set. So
 * tfw_hal_uart_tx_try only queues the byte and sets TIE, and the service
 * routine sends while the register is empty and the ring is not, and clears
 * TIE when the ring runs dry. Setting TIE is the one critical section: the
 * routine read-modify-writes CTRL as well.
 *
 * The pins are the board's (TFW_MCXN_LINK_* in board_profile.h); hal_soc.c
 * has already clocked their pin controller.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "lpuart_baud.h"

#define TFW_UART_RX_RING 64u
#define TFW_UART_TX_RING 64u

/* FRO12M, the LPUART's functional clock (TFW_MCXN_FCCLKSEL_FRO12M). */
#define LINK_CLK_HZ 12000000uL

_Static_assert((TFW_UART_RX_RING & (TFW_UART_RX_RING - 1u)) == 0u,
               "TFW_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert((TFW_UART_TX_RING & (TFW_UART_TX_RING - 1u)) == 0u,
               "TFW_UART_TX_RING must be a power of two: the index wraps by masking");
_Static_assert(TFW_UART_RX_RING <= 256u && TFW_UART_TX_RING <= 256u,
               "ring indices must fit in a uint8_t");
_Static_assert(TFW_MCXN_LINK_FC == 4u && TFW_MCXN_LINK_IRQ == 39u,
               "the service routine below is LP_FLEXCOMM4's vector, tfw_cm_irq39");
_Static_assert(TFW_MCXN_LINK_PORT < TFW_MCXN_PORT_COUNT &&
               TFW_MCXN_LINK_RX_BIT < 32u && TFW_MCXN_LINK_TX_BIT < 32u,
               "the board's link pins are not pins of this part");

void tfw_cm_irq39(void);

static volatile uint8_t rx_buf[TFW_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */

static volatile uint8_t tx_buf[TFW_UART_TX_RING];
static volatile uint8_t tx_head, tx_tail;   /* head: main writes. tail: ISR writes. */

tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    uint32_t reg;
    uint32_t fn = TFW_MCXN_LINK_PIN_FN << TFW_MCXN_PCR_MUX_SHIFT;

    if (tfw_mcxn_lpuart_baud(LINK_CLK_HZ, baud, &reg) != TFW_OK) {
        return TFW_ERR_RANGE;
    }
    tfw_cm_nvic_disable(TFW_MCXN_LINK_IRQ);

    TFW_MCXN_AHBCLKCTRLSET(1) = TFW_MCXN_CTRL1_FC(TFW_MCXN_LINK_FC);
    TFW_MCXN_PRESETCTRLCLR(1) = TFW_MCXN_CTRL1_FC(TFW_MCXN_LINK_FC);
    TFW_MCXN_FCCLKSEL(TFW_MCXN_LINK_FC) = TFW_MCXN_FCCLKSEL_FRO12M;
    TFW_MCXN_FLEXCOMMCLKDIV(TFW_MCXN_LINK_FC) = TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT;
    TFW_MCXN_FLEXCOMMCLKDIV(TFW_MCXN_LINK_FC) = 0u;
    TFW_MCXN_FC_PSELID = TFW_MCXN_FC_PSELID_UART;

    TFW_MCXN_LPUART_GLOBAL = TFW_MCXN_LPUART_GLOBAL_RST;
    TFW_MCXN_LPUART_GLOBAL = 0u;
    TFW_MCXN_LPUART_BAUD   = reg;
    TFW_MCXN_LPUART_STAT   = TFW_MCXN_LPUART_STAT_OR | TFW_MCXN_LPUART_STAT_NF |
                             TFW_MCXN_LPUART_STAT_FE;

    TFW_MCXN_PORT_PCR(TFW_MCXN_LINK_PORT, TFW_MCXN_LINK_RX_BIT) = fn | TFW_MCXN_PCR_IBE;
    TFW_MCXN_PORT_PCR(TFW_MCXN_LINK_PORT, TFW_MCXN_LINK_TX_BIT) = fn;

    rx_head = rx_tail = tx_head = tx_tail = 0u;
    TFW_MCXN_LPUART_CTRL = TFW_MCXN_LPUART_CTRL_RE | TFW_MCXN_LPUART_CTRL_TE |
                           TFW_MCXN_LPUART_CTRL_RIE;
    tfw_cm_nvic_enable(TFW_MCXN_LINK_IRQ);
    return TFW_OK;
}

void tfw_cm_irq39(void)
{
    uint32_t stat = TFW_MCXN_LPUART_STAT;

    while ((stat & TFW_MCXN_LPUART_STAT_RDRF) != 0u) {
        uint8_t d    = (uint8_t)TFW_MCXN_LPUART_DATA;
        uint8_t next = (uint8_t)((rx_head + 1u) & (TFW_UART_RX_RING - 1u));

        /* A full ring drops the byte, as on AVR: the framer resynchronises on
         * a frame that fails its check, which beats a frame silently mended
         * with the wrong bytes. */
        if (next != rx_tail) {
            rx_buf[rx_head] = d;
            rx_head         = next;
        }
        stat = TFW_MCXN_LPUART_STAT;
    }
    /* An overrun stops the receiver storing until it is cleared; the lost
     * byte is the framer's to notice, as above. */
    TFW_MCXN_LPUART_STAT = stat & (TFW_MCXN_LPUART_STAT_OR | TFW_MCXN_LPUART_STAT_NF |
                                   TFW_MCXN_LPUART_STAT_FE);

    while ((TFW_MCXN_LPUART_STAT & TFW_MCXN_LPUART_STAT_TDRE) != 0u && tx_tail != tx_head) {
        TFW_MCXN_LPUART_DATA = tx_buf[tx_tail];
        tx_tail = (uint8_t)((tx_tail + 1u) & (TFW_UART_TX_RING - 1u));
    }
    if (tx_tail == tx_head) {
        TFW_MCXN_LPUART_CTRL &= ~TFW_MCXN_LPUART_CTRL_TIE;
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
    TFW_MCXN_LPUART_CTRL |= TFW_MCXN_LPUART_CTRL_TIE;
    tfw_cm_irq_restore(primask);
    return 1u;
}

/* An empty ring and the transmitter idle (TC: nothing in the data register
 * and nothing shifting): the deferred reset waits on this so the
 * acknowledgement's last byte is out before the reset. */
uint8_t tfw_hal_uart_tx_idle(void)
{
    return (uint8_t)(tx_head == tx_tail &&
                     (TFW_MCXN_LPUART_STAT & TFW_MCXN_LPUART_STAT_TC) != 0u);
}
