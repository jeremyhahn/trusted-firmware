/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's UART on mps2: UART0, the CMSDK APB UART the application drives
 * (src/hal/cortex-m/mps2/hal_uart.c), polled. boot/boot_hal.h says why the
 * bootloader polls; here it also means tfw-boot enables no interrupt at all,
 * so its vector table holds no live entry the application could inherit.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "../../../boot_hal.h"
#include "../boot_soc.h"
#include "mps2/soc_regs.h"

void tfw_boot_uart_init(uint32_t baud)
{
    /* The divisor the application computes. A rate this clock cannot divide
     * to is a build-time constant (TFW_UART_BAUD) and the application refuses
     * it at init; here the UART is left off, so the session is silent rather
     * than garbled. */
    uint32_t div = (baud == 0u) ? 0u : (uint32_t)(TFW_CM_CPU_HZ / baud);

    TFW_MPS2_UART_CTRL = 0u;
    if (div < TFW_MPS2_UART_BAUDDIV_MIN) {
        return;
    }
    TFW_MPS2_UART_BAUDDIV = div;
    TFW_MPS2_UART_STATE   = TFW_MPS2_UART_STATE_TXOVR | TFW_MPS2_UART_STATE_RXOVR;
    TFW_MPS2_UART_CTRL    = TFW_MPS2_UART_CTRL_TXEN | TFW_MPS2_UART_CTRL_RXEN;
}

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    if ((TFW_MPS2_UART_STATE & TFW_MPS2_UART_STATE_RXFULL) == 0u) {
        return 0u;
    }
    *out = (uint8_t)TFW_MPS2_UART_DATA;
    return 1u;
}

void tfw_boot_uart_tx(uint8_t byte)
{
    if ((TFW_MPS2_UART_CTRL & TFW_MPS2_UART_CTRL_TXEN) == 0u) {
        return;                         /* never enabled: TXFULL would never clear */
    }
    while ((TFW_MPS2_UART_STATE & TFW_MPS2_UART_STATE_TXFULL) != 0u) { }
    TFW_MPS2_UART_DATA = byte;
}

void tfw_cm_boot_uart_off(void)
{
    if ((TFW_MPS2_UART_CTRL & TFW_MPS2_UART_CTRL_TXEN) != 0u) {
        while ((TFW_MPS2_UART_STATE & TFW_MPS2_UART_STATE_TXFULL) != 0u) { }
    }
    TFW_MPS2_UART_CTRL    = 0u;
    TFW_MPS2_UART_BAUDDIV = 0u;
    TFW_MPS2_UART_STATE   = TFW_MPS2_UART_STATE_TXOVR | TFW_MPS2_UART_STATE_RXOVR;
    TFW_MPS2_UART_INT     = TFW_MPS2_UART_INT_TX | TFW_MPS2_UART_INT_RX;
}
