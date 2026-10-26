/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's UART on the RP2350: UART0, the PL011 the application drives
 * (src/hal/cortex-m/rp2350/hal_uart.c) on the same pins with the same
 * divisors, polled. boot/boot_hal.h says why the bootloader polls; here it
 * also means tfw-boot enables no interrupt, so its vector table holds no live
 * entry the application could inherit.
 *
 * A block held in reset is not to be touched, so whether the UART is out of
 * reset is a flag here rather than a register read. A rate the clock cannot
 * divide to leaves it in reset: the session is silent rather than garbled, as
 * on mps2-tz.
 *
 * Against [RP-DS] release 8: the PL011's registers and bits §12.1.8, tables
 * 1081-1089; the divisors and their latch on the LCR_H write §12.1.7.1; the
 * reset bit §7.5.3 table 566; GPIO0/1 function 2 (UART0 TX/RX) §9.4 table
 * 677. test/test_boot_rp2350_uart.c runs it over a register model.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "../../../boot_hal.h"
#include "../boot_soc.h"
#include "rp2350/soc_regs.h"
#include "rp2350/soc_calc.h"

static uint8_t uart_on;

void tfw_boot_uart_init(uint32_t baud)
{
    uint32_t ibrd;
    uint32_t fbrd;

    TFW_RP_SET(TFW_RP_RESETS_RESET) = TFW_RP_RESET_UART0;
    uart_on = 0u;
    if (!tfw_rp_uart_divisors((uint32_t)TFW_CM_CPU_HZ, baud, &ibrd, &fbrd)) {
        return;
    }
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
    TFW_RP_UART_IMSC  = 0u;
    TFW_RP_UART_ICR   = TFW_RP_UART_ICR_ALL;
    TFW_RP_UART_CR    = TFW_RP_UART_CR_ON;
    uart_on = 1u;
}

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    if (!uart_on || (TFW_RP_UART_FR & TFW_RP_UART_FR_RXFE) != 0u) {
        return 0u;
    }
    *out = (uint8_t)TFW_RP_UART_DR;
    return 1u;
}

void tfw_boot_uart_tx(uint8_t byte)
{
    if (!uart_on) {
        return;                         /* never enabled: TXFF would never clear */
    }
    while ((TFW_RP_UART_FR & TFW_RP_UART_FR_TXFF) != 0u) {
    }
    TFW_RP_UART_DR = byte;
}

/* The last byte out of the shifter, then the UART back into reset, which
 * puts every register of it back to its reset value, and its two pins back
 * to no function. The application's init takes it out of reset again. */
void tfw_cm_boot_uart_off(void)
{
    if (uart_on) {
        while ((TFW_RP_UART_FR & TFW_RP_UART_FR_TXFE) == 0u ||
               (TFW_RP_UART_FR & TFW_RP_UART_FR_BUSY) != 0u) {
        }
    }
    TFW_RP_IO_CTRL(TFW_RP_UART0_TX_GPIO) = TFW_RP_FUNC_NULL;
    TFW_RP_IO_CTRL(TFW_RP_UART0_RX_GPIO) = TFW_RP_FUNC_NULL;
    TFW_RP_SET(TFW_RP_RESETS_RESET) = TFW_RP_RESET_UART0;
    uart_on = 0u;
}
