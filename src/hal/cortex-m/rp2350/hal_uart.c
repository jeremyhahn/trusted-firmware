/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link on UART0, an Arm PL011, on GPIO0 (TX) and GPIO1 (RX): the
 * Pico 2 W's header pins 1 and 2.
 *
 * Receive is interrupt-driven into the single-producer/single-consumer ring
 * src/hal/avr/hal_uart.c has, for its reasons; uint8_t indices keep it
 * lock-free, a byte load or store being one instruction here too. The UART
 * raises its receive interrupt at the FIFO's trigger level and its timeout
 * interrupt when bytes sit below that level for 32 bit periods, so a short
 * frame is not left in the FIFO.
 *
 * Transmit needs no ring: the PL011 has a 32-byte FIFO, which is the ring.
 * tfw_hal_uart_tx_try puts a byte in it when it has room and returns 0 when
 * it is full, and the caller retries on its next poll, which is the contract
 * tfw/hal.h states. With no transmit interrupt there is no critical section.
 *
 * The baud divisors are soc_calc.h's, from clk_peri, which hal_soc.c runs
 * from the crystal at TFW_CM_CPU_HZ.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_calc.h"

#define TFW_UART_RX_RING 64u

_Static_assert((TFW_UART_RX_RING & (TFW_UART_RX_RING - 1u)) == 0u,
               "TFW_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert(TFW_UART_RX_RING <= 256u, "ring indices must fit in a uint8_t");

void tfw_cm_irq33(void);

static volatile uint8_t rx_buf[TFW_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */

tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    uint32_t ibrd;
    uint32_t fbrd;

    if (!tfw_rp_uart_divisors((uint32_t)TFW_CM_CPU_HZ, baud, &ibrd, &fbrd)) {
        return TFW_ERR_RANGE;
    }
    tfw_cm_nvic_disable(TFW_RP_UART0_IRQ);
    TFW_RP_SET(TFW_RP_RESETS_RESET) = TFW_RP_RESET_UART0;
    TFW_RP_CLR(TFW_RP_RESETS_RESET) = TFW_RP_RESET_UART0;
    while ((TFW_RP_RESETS_DONE & TFW_RP_RESET_UART0) == 0u) {
    }

    TFW_CM_REG(TFW_RP_PADS_GPIO(TFW_RP_UART0_TX_GPIO)) = tfw_rp_pad_value(0u);
    TFW_CM_REG(TFW_RP_PADS_GPIO(TFW_RP_UART0_RX_GPIO)) = tfw_rp_pad_value(1u);
    TFW_RP_IO_CTRL(TFW_RP_UART0_TX_GPIO) = TFW_RP_FUNC_UART;
    TFW_RP_IO_CTRL(TFW_RP_UART0_RX_GPIO) = TFW_RP_FUNC_UART;

    /* The divisors latch on the LCR_H write that follows them. */
    TFW_RP_UART_IBRD  = ibrd;
    TFW_RP_UART_FBRD  = fbrd;
    TFW_RP_UART_LCR_H = TFW_RP_UART_LCR_H_8N1_FIFO;
    rx_head = rx_tail = 0u;
    TFW_RP_UART_ICR  = TFW_RP_UART_ICR_ALL;
    TFW_RP_UART_IMSC = TFW_RP_UART_IM_RX;
    TFW_RP_UART_CR   = TFW_RP_UART_CR_ON;
    tfw_cm_nvic_enable(TFW_RP_UART0_IRQ);
    return TFW_OK;
}

/* Reading the FIFO empty clears both receive interrupts. */
void tfw_cm_irq33(void)
{
    while ((TFW_RP_UART_FR & TFW_RP_UART_FR_RXFE) == 0u) {
        uint8_t d    = (uint8_t)TFW_RP_UART_DR;
        uint8_t next = (uint8_t)((rx_head + 1u) & (TFW_UART_RX_RING - 1u));

        /* A full ring drops the byte, as on AVR: the framer resynchronises on
         * a frame that fails its check, which beats a frame silently mended
         * with the wrong bytes. */
        if (next != rx_tail) {
            rx_buf[rx_head] = d;
            rx_head         = next;
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
    if ((TFW_RP_UART_FR & TFW_RP_UART_FR_TXFF) != 0u) {
        return 0u;                      /* full: the caller retries next poll */
    }
    TFW_RP_UART_DR = byte;
    return 1u;
}

/* The FIFO empty and the shifter done: the deferred reset waits on this so
 * the acknowledgement's last byte is out before the reset. */
uint8_t tfw_hal_uart_tx_idle(void)
{
    uint32_t fr = TFW_RP_UART_FR;

    return (uint8_t)((fr & TFW_RP_UART_FR_TXFE) != 0u &&
                     (fr & TFW_RP_UART_FR_BUSY) == 0u);
}
