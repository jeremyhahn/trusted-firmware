/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bench bake image (#7). Bakes the one block it carries, prints one line,
 * halts. Built by `make platform-bake BOARD=<board> SERIAL=<hex>`; see
 * docs/device-provisioning/engineering.md.
 *
 * It halts rather than resets: a reset would run it again, and the second run
 * would only report `already`. Opening the serial port on a board whose DTR
 * resets the part does exactly that, which is harmless and is why a second
 * line saying `already` after `baked` is the expected sight on a bench.
 */
#include "tfw/hal.h"
#include "tfw/config.h"
#include "bake.h"

int main(void)
{
    tfw_bake_result_t r;

    tfw_hal_init();
    tfw_hal_uart_init(TFW_UART_BAUD);
    tfw_hal_irq_enable();

    r = tfw_bake_run(tfw_bake_block);
    tfw_bake_report(TFW_BOARD_NAME, r, tfw_bake_block);
    while (!tfw_hal_uart_tx_idle()) {
    }
    for (;;) {
    }
}
