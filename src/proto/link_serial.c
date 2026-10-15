/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The link for an image with only the UART: every byte is serial and every
 * command is admitted, exactly what the transport did before it had a link.
 */

#include "tfw/link.h"
#include "tfw/hal.h"

void tfw_link_poll(void)
{
}

uint8_t tfw_link_rx(uint8_t *out, uint8_t hold)
{
    (void)hold;
    return tfw_hal_uart_rx(out);
}

uint8_t tfw_link_tx_try(uint8_t byte, uint8_t serial)
{
    (void)serial;
    return tfw_hal_uart_tx_try(byte);
}

void tfw_link_tx_done(uint8_t serial)
{
    (void)serial;
}

uint8_t tfw_link_admits(uint8_t cmd)
{
    (void)cmd;
    return 1u;
}
