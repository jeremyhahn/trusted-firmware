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

#include "tfw/hal.h"
#include "tfw/crypto.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "proto/transport.h"
#include "caps/sys.h"
#include "caps/sched.h"
#include "caps/bus.h"
#include "core/handoff.h"
#include "core/platform_id.h"
#include "core/auth_session.h"

tfw_status_t tfw_hal_tick_init(void);

static tfw_transport_t transport;

/* Everything that runs once, before the superloop.
 *
 * Kept out of line on purpose. main() never returns, so under -flto any
 * initializer inlined into it leaves its locals in main's frame for the life
 * of the device: tfw_platform_id_init's 128-byte EEPROM image and the element
 * provider's CDI buffer cost 241 bytes of a 2 KB part's stack that no command
 * could ever use. Called through a real frame, that scratch is released before
 * the first byte arrives. */
static void startup(void) __attribute__((noinline));

static void startup(void)
{
    tfw_hal_init();
    tfw_hal_tick_init();
    tfw_hal_uart_init(TFW_UART_BAUD);
    /* Every peripheral the HAL exports an init for gets one, unconditionally,
     * from HERE -- rather than the previous state, where none had a caller and
     * ADC reads were a plausible-looking zero on a converter that had never
     * run (#19) while the first SPI_XFER hung the device forever (#16).
     *
     * tfw_hal_adc_init() is safe to call with no gate: src/caps/analog.c and
     * hal_adc.c compile on every board this family builds (see mk/avr.mk).
     * tfw_bus_init() (src/caps/bus.c) covers I2C and SPI the same way its own
     * header comment explains -- it or bus_none.c's stub is ALWAYS the one
     * linked, so the call below is never a dangling reference regardless of
     * which buses a board has.
     *
     * This runs BEFORE tfw_crypto_init(): the ATECC608A provider calls
     * tfw_hal_i2c_init() again, at its own required rate, and that call must be
     * the one left standing on an element build rather than being clobbered
     * by the generic default that follows it. */
    tfw_hal_adc_init();
    tfw_bus_init();

    /* Both consuming reads, and both before tfw_crypto_init(): the crypto
     * provider's own init is what takes the handed-off CDI (src/core/
     * handoff.c), and DeviceID has to be in place before anything can build
     * evidence around it. See issues #6 and #18. */
    tfw_platform_id_init();
    tfw_boot_handoff_init();

    tfw_crypto_init();
    tfw_sched_init();
    tfw_auth_session_init();
    tfw_transport_init(&transport);
}

int main(void)
{
    startup();
    sei();

    /* Announce ourselves. An unexpected boot event is how a host learns the
     * device reset underneath it, which invalidates everything it believed
     * about the device's state. */
    (void)tfw_transport_send_event(&transport, TFW_EVENT_BOOT);

    for (;;) {
        tfw_transport_poll(&transport);
        /* SYS_INFO cannot reach the framer itself -- dispatch.h forbids it --
         * so the one module that already sees both hands the counters across. */
        tfw_sys_set_framer_errors(transport.framer.err_crc, transport.framer.err_format);
        tfw_sched_poll();
        tfw_sys_poll();
    }
}
