/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The superloop.
 *
 * There is no delay() in this firmware and there must never be one. Every poll
 * below returns promptly, so a timed output fires within one loop iteration --
 * tens of microseconds -- rather than waiting for a sleep to end. The firmware
 * this replaces blocked for 2000 ms every pass and another 100 ms per request,
 * which meant a scheduled pin could overrun by two seconds and the device could
 * not answer while it waited.
 */

#include <avr/interrupt.h>

#include "ag/hal.h"
#include "ag/crypto.h"
#include "ag/config.h"
#include "ag/protocol.h"
#include "proto/transport.h"
#include "caps/sys.h"
#include "caps/sched.h"

ag_status_t ag_hal_tick_init(void);

static ag_transport_t transport;

int main(void)
{
    ag_hal_init();
    ag_hal_tick_init();
    ag_hal_uart_init(AG_UART_BAUD);
    ag_crypto_init();
    ag_sched_init();
    ag_transport_init(&transport);

    sei();

    /* Announce ourselves. An unexpected boot event is how a host learns the
     * device reset underneath it, which invalidates everything it believed
     * about the device's state. */
    (void)ag_transport_send_event(&transport, AG_EVENT_BOOT);

    for (;;) {
        ag_transport_poll(&transport);
        /* SYS_INFO cannot reach the framer itself -- dispatch.h forbids it --
         * so the one module that already sees both hands the counters across. */
        ag_sys_set_framer_errors(transport.framer.err_crc, transport.framer.err_format);
        ag_sched_poll();
        ag_sys_poll();
    }
}
