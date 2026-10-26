/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's polled LPUART on the MCXN947 (mcxn_uart.h). The register order
 * is hal_uart.c's: function select, the LPUART's software reset, the rate,
 * the error flags cleared, then the receiver and transmitter on.
 */

#include <stdint.h>

#include "mcxn_uart.h"
#include "mcxn94x/lpuart_baud.h"
#include "mcxn94x/soc_regs.h"

#define FC_REG(fc, off) ((fc)[(off) / 4u])

#define STAT_ERRORS (TFW_MCXN_LPUART_STAT_OR | TFW_MCXN_LPUART_STAT_NF | \
                     TFW_MCXN_LPUART_STAT_FE)

tfw_status_t tfw_mcxn_uart_start(volatile uint32_t *fc, uint32_t clk_hz, uint32_t baud)
{
    uint32_t reg = 0u;

    if (tfw_mcxn_lpuart_baud(clk_hz, baud, &reg) != TFW_OK) {
        return TFW_ERR_RANGE;
    }
    FC_REG(fc, TFW_MCXN_FC_PSELID_OFF)     = TFW_MCXN_FC_PSELID_UART;
    FC_REG(fc, TFW_MCXN_LPUART_GLOBAL_OFF) = TFW_MCXN_LPUART_GLOBAL_RST;
    FC_REG(fc, TFW_MCXN_LPUART_GLOBAL_OFF) = 0u;
    FC_REG(fc, TFW_MCXN_LPUART_BAUD_OFF)   = reg;
    FC_REG(fc, TFW_MCXN_LPUART_STAT_OFF)   = STAT_ERRORS;   /* write 1 to clear */
    FC_REG(fc, TFW_MCXN_LPUART_CTRL_OFF)   = TFW_MCXN_LPUART_CTRL_RE | TFW_MCXN_LPUART_CTRL_TE;
    return TFW_OK;
}

uint8_t tfw_mcxn_uart_getc(volatile uint32_t *fc, uint8_t *out)
{
    uint32_t stat = FC_REG(fc, TFW_MCXN_LPUART_STAT_OFF);

    if ((stat & STAT_ERRORS) != 0u) {
        FC_REG(fc, TFW_MCXN_LPUART_STAT_OFF) = stat & STAT_ERRORS;
    }
    if ((stat & TFW_MCXN_LPUART_STAT_RDRF) == 0u) {
        return 0u;
    }
    *out = (uint8_t)FC_REG(fc, TFW_MCXN_LPUART_DATA_OFF);
    return 1u;
}

void tfw_mcxn_uart_putc(volatile uint32_t *fc, uint8_t byte)
{
    while ((FC_REG(fc, TFW_MCXN_LPUART_STAT_OFF) & TFW_MCXN_LPUART_STAT_TDRE) == 0u) {
    }
    FC_REG(fc, TFW_MCXN_LPUART_DATA_OFF) = byte;
}

void tfw_mcxn_uart_stop(volatile uint32_t *fc)
{
    while ((FC_REG(fc, TFW_MCXN_LPUART_STAT_OFF) & TFW_MCXN_LPUART_STAT_TC) == 0u) {
    }
    FC_REG(fc, TFW_MCXN_LPUART_CTRL_OFF)   = 0u;
    FC_REG(fc, TFW_MCXN_LPUART_GLOBAL_OFF) = TFW_MCXN_LPUART_GLOBAL_RST;
    FC_REG(fc, TFW_MCXN_FC_PSELID_OFF)     = 0u;
}
