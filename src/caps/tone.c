/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Tone, in its own translation unit.
 *
 * Not because it is large, but because it is OPTIONAL and the rule in this tree
 * is that a feature is gated by including or excluding a whole file rather than
 * by #if inside one. The ATmega32U4 has no Timer2 at all, so it sets
 * AG_FEATURE_TONE 0 and this file is simply not compiled -- which is the only
 * arrangement under which the build that ships is the build that was tested.
 */

#include "pwm.h"
#include "ag/board.h"
#include "ag/config.h"
#include "ag/hal.h"

ag_status_t ag_tone_cmd_start(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin = ag_read_u8(r);
    uint16_t    hz  = ag_read_u16(r);
    ag_pinref_t ref;
    ag_status_t st;

    (void)w;
    /* Tone is one timer driving one output-compare pin. Any other pin would
     * need the timer's other channel or bit-banging, and answering AG_OK for a
     * pin that will never make a sound is worse than refusing: the host has no
     * way to discover that its speaker is on the wrong leg. */
    if (pin != AG_TONE_PIN) {
        return AG_ERR_UNSUPPORTED;
    }
    if (ag_board_pin_flags(pin) & (AG_PIN_RESERVED | AG_PIN_IN_USE)) {
        return AG_ERR_RESERVED_PIN;
    }
    st = ag_board_pin(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    return ag_hal_tone_start(&ref, hz);
}

ag_status_t ag_tone_cmd_stop(ag_reader_t *r, ag_writer_t *w)
{
    (void)r; (void)w;
    return ag_hal_tone_stop();
}
