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

#include <avr/io.h>
#include <util/atomic.h>
#include "tfw/hal.h"
#include "tfw/config.h"

static uint8_t tone_active;

/*
 * Tone is an 8-bit Timer2 path, and only that.
 *
 * A 16-bit timer would need a different waveform-generation mode (CTC lives in
 * TCCRnB for those) and a different prescaler table -- clk/64 is CS22 on
 * Timer2 and CS31 on Timer3. Sharing this code between them would produce a
 * tone at the wrong frequency rather than no tone, which is the failure that
 * gets shipped. A board whose tone timer is not Timer2 sets TFW_FEATURE_TONE 0
 * and this file is not linked at all; the assert is here so that choosing a
 * different timer and forgetting to turn the feature off is a build error.
 */
_Static_assert(TFW_TONE_TIMER == 2u,
               "the tone path drives Timer2; a board using another timer must "
               "set TFW_FEATURE_TONE 0 until a 16-bit path exists");

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    uint32_t top;

    if (hz == 0u) {
        return tfw_hal_tone_stop();
    }
    /* CTC on Timer2 with the pin toggling on compare: the output flips once per
     * match, so the wave is half the match rate. Prescaler 64 keeps the whole
     * audible range reachable with an 8-bit compare value. */
    top = (F_CPU / (2UL * 64UL * (uint32_t)hz)) - 1UL;
    if (top > 255UL) {
        return TFW_ERR_RANGE;   /* below about 490 Hz at this prescaler */
    }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        /* The direction bit comes from the pin the CALLER resolved, not from a
         * literal. DDRB bit 3 is OC2A on a 328P and MISO on a Mega, where OC2A
         * is PB4 -- so the literal version set the direction of one pin while
         * the timer drove another, and the tone simply did not sound. The
         * board descriptor already knows where OC2A is; use what it said. */
        _SFR_MEM8(p->port + 1u) |= (uint8_t)_BV(p->bit);
        TCCR2A = (uint8_t)(_BV(COM2A0) | _BV(WGM21));
        TCCR2B = (uint8_t)_BV(CS22);
        OCR2A  = (uint8_t)top;
    }
    tone_active = 1u;
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        TCCR2A = (uint8_t)(TCCR2A & (uint8_t)~_BV(COM2A0));
        TCCR2B = 0u;
    }
    tone_active = 0u;
    return TFW_OK;
}
