/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <util/delay.h>
#include "tfw/hal.h"
#include "tfw/config.h"

#define PIN_REG(p) _SFR_MEM8((p)->port)

static uint8_t level_of(const tfw_pinref_t *p)
{
    return (uint8_t)((PIN_REG(p) & (uint8_t)(1u << p->bit)) != 0u);
}

/*
 * Busy-measured, and interrupts stay ENABLED throughout.
 *
 * Disabling them would give a cleaner measurement and cost the millisecond tick
 * and every byte arriving on the UART -- a trade that buys precision nobody
 * needs and loses the device's ability to be talked to. The jitter an interrupt
 * introduces is a few microseconds on a signal whose shortest meaningful pulse
 * is twenty-six.
 */
tfw_status_t tfw_hal_pulse_in(const tfw_pinref_t *p, uint8_t level,
                            uint32_t timeout_us, uint32_t *out_us)
{
    uint32_t waited = 0u;
    uint32_t width  = 0u;

    while (level_of(p) == level) {          /* wait out a pulse in progress */
        if (++waited > timeout_us) { return TFW_ERR_TIMEOUT; }
        _delay_us(1);
    }
    while (level_of(p) != level) {          /* wait for the edge */
        if (++waited > timeout_us) { return TFW_ERR_TIMEOUT; }
        _delay_us(1);
    }
    while (level_of(p) == level) {          /* measure */
        if (++width > timeout_us) { return TFW_ERR_TIMEOUT; }
        _delay_us(1);
    }
    *out_us = width;
    return TFW_OK;
}

tfw_status_t tfw_hal_pulse_capture(const tfw_pinref_t *p, uint8_t start_level,
                                 uint16_t *out_us, uint8_t max_edges,
                                 uint32_t timeout_us, uint8_t *out_n)
{
    uint8_t  want = start_level;
    uint8_t  n    = 0u;
    uint32_t idle = 0u;

    while (level_of(p) != want) {           /* line up on the first edge */
        if (++idle > timeout_us) { return TFW_ERR_TIMEOUT; }
        _delay_us(1);
    }

    while (n < max_edges) {
        uint32_t w = 0u;
        want = (uint8_t)!want;
        while (level_of(p) != want) {
            if (++w > timeout_us) {
                /* A quiet line ends the capture rather than failing it: the
                 * edges already recorded are the message, and a sensor that
                 * stops talking after forty bits has said everything it had
                 * to say. */
                *out_n = n;
                return n ? TFW_OK : TFW_ERR_TIMEOUT;
            }
            _delay_us(1);
        }
        out_us[n++] = (uint16_t)(w > 65535u ? 65535u : w);
    }
    *out_n = n;
    return TFW_OK;
}
