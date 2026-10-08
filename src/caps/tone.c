/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Tone, in its own translation unit.
 *
 * Not because it is large, but because it is OPTIONAL and the rule in this tree
 * is that a feature is gated by including or excluding a whole file rather than
 * by #if inside one. The ATmega32U4 has no Timer2 at all, so it sets
 * TFW_FEATURE_TONE 0 and this file is simply not compiled -- which is the only
 * arrangement under which the build that ships is the build that was tested.
 */

#include "pwm.h"
#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/hal.h"

tfw_status_t tfw_tone_cmd_start(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin = tfw_read_u8(r);
    uint16_t    hz  = tfw_read_u16(r);
    tfw_pinref_t ref;
    tfw_status_t st;

    (void)w;
    /* Tone is one timer driving one output-compare pin. Any other pin would
     * need the timer's other channel or bit-banging, and answering TFW_OK for a
     * pin that will never make a sound is worse than refusing: the host has no
     * way to discover that its speaker is on the wrong leg. */
    if (pin != TFW_TONE_PIN) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (tfw_board_pin_flags(pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
        return TFW_ERR_RESERVED_PIN;
    }
    st = tfw_board_pin(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    return tfw_hal_tone_start(&ref, hz);
}

tfw_status_t tfw_tone_cmd_stop(tfw_reader_t *r, tfw_writer_t *w)
{
    (void)r; (void)w;
    return tfw_hal_tone_stop();
}
