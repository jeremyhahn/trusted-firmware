/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tone's TIMER arithmetic on nrf54l, kept apart from the register writes
 * (hal_tone.c) so a host test pins it alone (test/unit/test_nrf54l_tone.c).
 *
 * The TIMER counts a 16 MHz clock divided by 2^PRESCALER, PRESCALER 0-9
 * (nrf54l15_types.h, TIMER_PRESCALER_PRESCALER_Max), in 16-bit mode. CC[0]
 * with the COMPARE0-to-CLEAR short makes a COMPARE0 every CC ticks, and each
 * one toggles the pin, so the wave's period is two of them.
 */
#ifndef TFW_CM_NRF54L_TONE_CALC_H
#define TFW_CM_NRF54L_TONE_CALC_H

#include <stdint.h>

#include "tfw/status.h"

#define TFW_NRF54L_TONE_CLOCK_HZ     16000000uL
#define TFW_NRF54L_TONE_PRESCALE_MAX 9u
#define TFW_NRF54L_TONE_CC_MIN       2uL      /* below this the wave is not the frequency asked */
#define TFW_NRF54L_TONE_CC_MAX       0xFFFFuL /* 16-bit mode */

/* The PRESCALER and CC[0] for a square wave of hz: the smallest prescaler
 * whose half-period, rounded to the nearest tick, fits 16 bits, which keeps
 * the most resolution. 16 MHz is 2^10 * 15625, so every prescaled clock is a
 * whole number of hertz. TFW_ERR_RANGE for 0 and for a frequency too high to
 * be two ticks apart. */
static inline tfw_status_t tfw_nrf54l_tone_calc(uint32_t hz, uint32_t *prescaler, uint32_t *cc)
{
    uint32_t p;

    if (hz == 0u || hz > TFW_NRF54L_TONE_CLOCK_HZ / 2u) {
        return TFW_ERR_RANGE;
    }
    for (p = 0u; p <= TFW_NRF54L_TONE_PRESCALE_MAX; p++) {
        uint32_t clk  = TFW_NRF54L_TONE_CLOCK_HZ >> p;
        uint32_t half = (clk + hz) / (2u * hz);   /* clk / 2hz, rounded */

        if (half <= TFW_NRF54L_TONE_CC_MAX) {
            if (half < TFW_NRF54L_TONE_CC_MIN) {
                return TFW_ERR_RANGE;
            }
            *prescaler = p;
            *cc        = half;
            return TFW_OK;
        }
    }
    return TFW_ERR_RANGE;   /* unreachable for hz >= 1: 15625 / 1 Hz fits */
}

#endif /* TFW_CM_NRF54L_TONE_CALC_H */
