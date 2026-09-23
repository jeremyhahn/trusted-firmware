/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "pulse.h"
#include "ag/board.h"
#include "ag/config.h"
#include "ag/hal.h"

/* Bounded by what a frame can carry: each interval is two bytes and the reply
 * also has a count. Capturing more than can be returned would be work thrown
 * away. */
#define AG_CAPTURE_MAX ((uint8_t)((AG_MAX_PAYLOAD - 1u) / 2u))

/* A ceiling on how long any single command may hold the loop. Past this, a
 * scheduled pin could miss its deadline -- and once a deadman exists, a long
 * capture would trip it. */
#ifndef AG_PULSE_MAX_US
#define AG_PULSE_MAX_US 50000UL
#endif

ag_status_t ag_pulse_cmd_in(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin     = ag_read_u8(r);
    uint8_t     level   = ag_read_u8(r);
    uint32_t    timeout = ag_read_u32(r);
    ag_pinref_t ref;
    ag_status_t st;
    uint32_t    us;

    st = ag_board_pin(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    if (level > 1u) {
        return AG_ERR_RANGE;
    }
    if (timeout == 0u || timeout > AG_PULSE_MAX_US) {
        return AG_ERR_RANGE;
    }
    st = ag_hal_pulse_in(&ref, level, timeout, &us);
    if (st != AG_OK) {
        return st;
    }
    ag_write_u32(w, us);
    return AG_OK;
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
ag_status_t ag_pulse_cmd_capture(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin       = ag_read_u8(r);
    uint8_t     start     = ag_read_u8(r);
    uint8_t     max_edges = ag_read_u8(r);
    uint32_t    timeout   = ag_read_u32(r);
    uint16_t    us[AG_CAPTURE_MAX];
    ag_pinref_t ref;
    ag_status_t st;
    uint8_t     n = 0u;

    st = ag_board_pin(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    if (start > 1u) {
        return AG_ERR_RANGE;
    }
    if (max_edges == 0u || max_edges > AG_CAPTURE_MAX) {
        return AG_ERR_RANGE;
    }
    if (timeout == 0u || timeout > AG_PULSE_MAX_US) {
        return AG_ERR_RANGE;
    }

    st = ag_hal_pulse_capture(&ref, start, us, max_edges, timeout, &n);
    if (st != AG_OK) {
        return st;
    }

    ag_write_u8(w, n);
    for (uint8_t i = 0u; i < n; i++) {
        ag_write_u16(w, us[i]);
    }
    return AG_OK;
}
