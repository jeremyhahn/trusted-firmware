/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tone's divider: the match value that makes a CTIMER, counting its clock
 * undivided (PR 0) and reset on the match, toggle its output at twice the
 * tone's frequency. The counter runs 0..MR, MR + 1 counts, so a full wave is
 * 2 * (MR + 1) clocks: MR + 1 = clk / (2 * hz), rounded to nearest. Pure
 * arithmetic, host-tested alone (test/unit/test_mcxn94x_tone.c).
 */
#ifndef TFW_CM_MCXN94X_CTIMER_TONE_H
#define TFW_CM_MCXN94X_CTIMER_TONE_H

#include <stdint.h>

#include "tfw/status.h"

/* A half period under two counts is refused: MR 0 would match on every clock,
 * which the device header does not say the timer toggles at. */
#define TFW_MCXN_TONE_MIN_HALF 2u

static inline tfw_status_t tfw_mcxn_tone_match(uint32_t clk_hz, uint16_t hz,
                                               uint32_t *mr)
{
    uint32_t two_hz;
    uint32_t half;

    if (hz == 0u || clk_hz == 0u) {
        return TFW_ERR_RANGE;
    }
    two_hz = 2u * (uint32_t)hz;
    half   = clk_hz / two_hz;
    if (clk_hz % two_hz >= two_hz - clk_hz % two_hz) {
        half++;     /* nearest, without the overflow clk_hz + hz could hit */
    }
    if (half < TFW_MCXN_TONE_MIN_HALF) {
        return TFW_ERR_RANGE;
    }
    *mr = half - 1u;
    return TFW_OK;
}

#endif /* TFW_CM_MCXN94X_CTIMER_TONE_H */
