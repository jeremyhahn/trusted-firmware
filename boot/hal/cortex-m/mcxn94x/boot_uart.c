/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's UART on the MCXN947: LPUART4 on LP_FLEXCOMM4, the unit the
 * application drives (src/hal/cortex-m/mcxn94x/hal_uart.c) on the same pins
 * from the same 12 MHz FRO, polled. boot/boot_hal.h says why the bootloader
 * polls; here it also means tfw-boot enables no interrupt, so its vector
 * table holds no live entry the application could inherit.
 *
 * tfw-boot does not run hal_soc.c's early setup, which clocks the pin
 * controllers in the application, so the pin controller for the link's port
 * is clocked here. The pins and their function are the SDK's
 * BOARD_InitPins (docs/boards/frdm-mcxn947.md).
 *
 * A rate the clock cannot divide to leaves the UART off: the session is
 * silent rather than garbled, as on the RP2350.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "../../../boot_hal.h"
#include "../boot_soc.h"
#include "mcxn_uart.h"
#include "mcxn94x/soc_regs.h"

#define LINK_CLK_HZ 12000000uL      /* FRO12M, as hal_uart.c */
#define LINK_FC     ((volatile uint32_t *)(uintptr_t)TFW_MCXN_FC4_BASE)

_Static_assert(TFW_MCXN_LINK_FC == 4u, "LINK_FC is LP_FLEXCOMM4's base");

static uint8_t uart_on;

void tfw_boot_uart_init(uint32_t baud)
{
    uart_on = 0u;
    TFW_MCXN_AHBCLKCTRLSET(0) = TFW_MCXN_CTRL0_PORT(TFW_MCXN_LINK_PORT);
    TFW_MCXN_PRESETCTRLCLR(0) = TFW_MCXN_CTRL0_PORT(TFW_MCXN_LINK_PORT);
    TFW_MCXN_AHBCLKCTRLSET(1) = TFW_MCXN_CTRL1_FC(TFW_MCXN_LINK_FC);
    TFW_MCXN_PRESETCTRLCLR(1) = TFW_MCXN_CTRL1_FC(TFW_MCXN_LINK_FC);
    TFW_MCXN_FCCLKSEL(TFW_MCXN_LINK_FC) = TFW_MCXN_FCCLKSEL_FRO12M;
    TFW_MCXN_FLEXCOMMCLKDIV(TFW_MCXN_LINK_FC) = TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT;
    TFW_MCXN_FLEXCOMMCLKDIV(TFW_MCXN_LINK_FC) = 0u;

    if (tfw_mcxn_uart_start(LINK_FC, LINK_CLK_HZ, baud) != TFW_OK) {
        return;
    }
    TFW_MCXN_PORT_PCR(TFW_MCXN_LINK_PORT, TFW_MCXN_LINK_RX_BIT) =
        (TFW_MCXN_LINK_PIN_FN << TFW_MCXN_PCR_MUX_SHIFT) | TFW_MCXN_PCR_IBE;
    TFW_MCXN_PORT_PCR(TFW_MCXN_LINK_PORT, TFW_MCXN_LINK_TX_BIT) =
        TFW_MCXN_LINK_PIN_FN << TFW_MCXN_PCR_MUX_SHIFT;
    uart_on = 1u;
}

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    if (!uart_on) {
        return 0u;
    }
    return tfw_mcxn_uart_getc(LINK_FC, out);
}

void tfw_boot_uart_tx(uint8_t byte)
{
    if (!uart_on) {
        return;                         /* never enabled: TDRE would never set */
    }
    tfw_mcxn_uart_putc(LINK_FC, byte);
}

/* The last byte out of the shifter, the LPUART held in its reset, and its
 * two pins back to function 0. The application's init takes it out again. */
void tfw_cm_boot_uart_off(void)
{
    if (uart_on) {
        tfw_mcxn_uart_stop(LINK_FC);
        TFW_MCXN_PORT_PCR(TFW_MCXN_LINK_PORT, TFW_MCXN_LINK_RX_BIT) = 0u;
        TFW_MCXN_PORT_PCR(TFW_MCXN_LINK_PORT, TFW_MCXN_LINK_TX_BIT) = 0u;
    }
    uart_on = 0u;
}
