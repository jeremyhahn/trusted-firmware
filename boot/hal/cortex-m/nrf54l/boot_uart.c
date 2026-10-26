/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's UART on nrf54l: UARTE20, the DK's console the application drives
 * (src/hal/cortex-m/nrf54l/hal_uart.c), polled. boot/boot_hal.h says why the
 * bootloader polls; here INTEN stays 0, so no event reaches the NVIC and
 * tfw-boot's vector table holds no live entry the application could inherit.
 *
 * A UARTE moves bytes only by EasyDMA, so what is polled is its events. A
 * one-byte receive transfer into rx_dma is always armed, and its end event is
 * the byte-waiting flag; the UARTE holds bytes that arrive before the next
 * transfer starts in its receive FIFO. A byte sent is a one-byte transfer
 * from tx_dma, waited on until TXDRDY, when it has left the TXD line, so the
 * last byte is out before tfw_cm_boot_uart_off stops anything.
 *
 * The waits are bounded, as hal_soc.c's are: a UARTE that never raises its
 * event costs the session, not the boot.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "../../../boot_hal.h"
#include "../boot_soc.h"
#include "nrf54l/soc_regs.h"
#include "nrf54l/soc_tables.h"

/* One byte at 1200 baud is about 1.1 million cycles at 128 MHz, and each pass
 * of a wait loop is several; this covers every rate the vendor's table lists. */
#define UARTE_SPINS 1000000u

/* EasyDMA's two one-byte buffers, in RAM as it requires. */
static volatile uint8_t rx_dma;
static volatile uint8_t tx_dma;
static uint8_t          enabled;

static uint8_t event_wait(const volatile uint32_t *event)
{
    for (uint32_t i = 0u; i < UARTE_SPINS; i++) {
        if (*event != 0u) {
            return 1u;
        }
    }
    return 0u;
}

static void rx_start(void)
{
    TFW_NRF_UARTE_RX_PTR    = (uint32_t)(uintptr_t)&rx_dma;
    TFW_NRF_UARTE_RX_MAXCNT = 1u;
    TFW_NRF_UARTE_RX_START  = 1u;
}

void tfw_boot_uart_init(uint32_t baud)
{
    uint32_t reg;

    /* A rate the vendor's table does not list is a build-time constant
     * (TFW_UART_BAUD) the application refuses at init; here the UARTE is
     * left off, so the session is silent rather than garbled. */
    TFW_NRF_UARTE_ENABLE = 0u;
    TFW_NRF_UARTE_INTEN  = 0u;
    enabled = 0u;
    if (tfw_nrf54l_uarte_baud(baud, &reg) != TFW_OK) {
        return;
    }

    /* The pins as the application sets them, for its reasons. */
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
    TFW_NRF_UARTE_RXTO     = 0u;
    TFW_NRF_UARTE_ERRORSRC = TFW_NRF_UARTE_ERRORSRC_ALL;

    TFW_NRF_UARTE_ENABLE = TFW_NRF_UARTE_ENABLE_ON;
    rx_start();
    enabled = 1u;
}

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    if (!enabled) {
        return 0u;
    }
    /* An overrun, framing error or break: cleared, and the byte kept or
     * dropped by the framer's check, as in the application. */
    if (TFW_NRF_UARTE_ERROR != 0u) {
        TFW_NRF_UARTE_ERROR    = 0u;
        TFW_NRF_UARTE_ERRORSRC = TFW_NRF_UARTE_ERRORSRC_ALL;
    }
    if (TFW_NRF_UARTE_RX_END == 0u) {
        return 0u;
    }
    *out = rx_dma;
    TFW_NRF_UARTE_RX_END = 0u;
    rx_start();
    return 1u;
}

void tfw_boot_uart_tx(uint8_t byte)
{
    if (!enabled) {
        return;                         /* never enabled: TXDRDY would never come */
    }
    tx_dma = byte;
    TFW_NRF_UARTE_TXDRDY    = 0u;
    TFW_NRF_UARTE_TX_END    = 0u;
    TFW_NRF_UARTE_TX_PTR    = (uint32_t)(uintptr_t)&tx_dma;
    TFW_NRF_UARTE_TX_MAXCNT = 1u;
    TFW_NRF_UARTE_TX_START  = 1u;
    (void)event_wait(&TFW_NRF_UARTE_TXDRDY);
    TFW_NRF_UARTE_TXDRDY = 0u;
    TFW_NRF_UARTE_TX_END = 0u;
}

void tfw_cm_boot_uart_off(void)
{
    if (enabled) {
        /* The armed receive stopped first: a disabled UARTE must not still
         * own a transfer into tfw-boot's RAM the application reuses. */
        TFW_NRF_UARTE_RX_STOP = 1u;
        (void)event_wait(&TFW_NRF_UARTE_RXTO);
        enabled = 0u;
    }
    TFW_NRF_UARTE_ENABLE   = 0u;
    TFW_NRF_UARTE_INTEN    = 0u;
    TFW_NRF_UARTE_PSEL_TXD = TFW_NRF54L_PSEL_DISCONNECTED;
    TFW_NRF_UARTE_PSEL_RXD = TFW_NRF54L_PSEL_DISCONNECTED;
    TFW_NRF_UARTE_PSEL_CTS = TFW_NRF54L_PSEL_DISCONNECTED;
    TFW_NRF_UARTE_PSEL_RTS = TFW_NRF54L_PSEL_DISCONNECTED;
    TFW_NRF_UARTE_BAUDRATE = TFW_NRF_UARTE_BAUDRATE_RESET;
    TFW_NRF_UARTE_CONFIG   = TFW_NRF_UARTE_CONFIG_RESET;
    TFW_NRF_UARTE_RX_END   = 0u;
    TFW_NRF_UARTE_TX_END   = 0u;
    TFW_NRF_UARTE_TXDRDY   = 0u;
    TFW_NRF_UARTE_ERROR    = 0u;
    TFW_NRF_UARTE_RXTO     = 0u;
    TFW_NRF_UARTE_ERRORSRC = TFW_NRF_UARTE_ERRORSRC_ALL;

    TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_CONSOLE_TX_BIT) = TFW_NRF_PIN_CNF_RESET;
    TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_CONSOLE_RX_BIT) = TFW_NRF_PIN_CNF_RESET;
    TFW_NRF_GPIO_OUTCLR(TFW_NRF_P1_BASE) = 1uL << TFW_NRF_CONSOLE_TX_BIT;
}
