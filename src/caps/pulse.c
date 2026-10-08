/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "pulse.h"
#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/hal.h"

/* Bounded by what a frame can carry: each interval is two bytes and the reply
 * also has a count. Capturing more than can be returned would be work thrown
 * away. */
#define TFW_CAPTURE_MAX ((uint8_t)((TFW_MAX_PAYLOAD - 1u) / 2u))

tfw_status_t tfw_pulse_cmd_in(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin     = tfw_read_u8(r);
    uint8_t     level   = tfw_read_u8(r);
    uint32_t    timeout = tfw_read_u32(r);
    tfw_pinref_t ref;
    tfw_status_t st;
    uint32_t    us;

    st = tfw_board_pin(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    if (level > 1u) {
        return TFW_ERR_RANGE;
    }
    if (timeout == 0u || timeout > TFW_PULSE_MAX_US) {
        return TFW_ERR_RANGE;
    }
    st = tfw_hal_pulse_in(&ref, level, timeout, &us);
    if (st != TFW_OK) {
        return st;
    }
    tfw_write_u32(w, us);
    return TFW_OK;
}

/*
 * Edge-interval capture: the primitive that makes timing-critical sensors work
 * without putting any sensor into the firmware.
 *
 * A DHT22 distinguishes a bit by a 26- versus 70-microsecond pulse. No protocol
 * with a round trip per bit can read that, so "the host decodes the sensor"
 * would be an empty claim without this. Capturing the intervals and letting the
 * host turn them into bits keeps the device generic -- and the same command
 * reads DHT11/22, HC-SR04, IR remotes and RC PPM, with no code here that knows
 * any of them exist.
 */
tfw_status_t tfw_pulse_cmd_capture(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin       = tfw_read_u8(r);
    uint8_t     start     = tfw_read_u8(r);
    uint8_t     max_edges = tfw_read_u8(r);
    uint32_t    timeout   = tfw_read_u32(r);
    uint16_t    us[TFW_CAPTURE_MAX];
    tfw_pinref_t ref;
    tfw_status_t st;
    uint8_t     n = 0u;

    st = tfw_board_pin(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    if (start > 1u) {
        return TFW_ERR_RANGE;
    }
    if (max_edges == 0u || max_edges > TFW_CAPTURE_MAX) {
        return TFW_ERR_RANGE;
    }
    if (timeout == 0u || timeout > TFW_PULSE_MAX_US) {
        return TFW_ERR_RANGE;
    }

    st = tfw_hal_pulse_capture(&ref, start, us, max_edges, timeout, &n);
    if (st != TFW_OK) {
        return st;
    }

    tfw_write_u8(w, n);
    for (uint8_t i = 0u; i < n; i++) {
        tfw_write_u16(w, us[i]);
    }
    return TFW_OK;
}
