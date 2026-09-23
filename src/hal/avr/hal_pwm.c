/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <util/atomic.h>
#include "ag/hal.h"
#include "ag/config.h"

#define REG8(a)  _SFR_MEM8(a)
#define REG16(a) _SFR_MEM16(a)

/*
 * Fast PWM, 8-bit, non-inverting.
 *
 * The timer is configured on first use rather than at boot: a board that never
 * calls pwm_write should not have its timers running, both for the power and
 * because a timer left counting is a timer someone else cannot use.
 */
ag_status_t ag_hal_pwm_write(const ag_pwmref_t *p, uint8_t duty)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        /* WGM(n)0 selects fast PWM 8-bit on both timer families here, and the
         * COM bit connects the pin to the compare output. */
        REG8(p->tccra) |= (uint8_t)(_BV(p->com_bit) | _BV(WGM00));

        if (p->is16) {
            /* A 16-bit OCR must be written as a word, low byte first, or the
             * high byte latches against a stale low byte and the duty jumps to
             * something nobody asked for. The compiler does this correctly for
             * a uint16_t store; writing the halves by hand is what goes wrong. */
            REG16(p->ocr) = duty;
        } else {
            REG8(p->ocr) = duty;
        }
    }
    return AG_OK;
}

ag_status_t ag_hal_pwm_stop(const ag_pwmref_t *p)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        /* Disconnect the pin from the compare unit and leave it a plain GPIO,
         * rather than setting a duty of zero -- which would hold the pin low
         * through the timer and quietly prevent digital_write from working. */
        REG8(p->tccra) = (uint8_t)(REG8(p->tccra) & (uint8_t)~_BV(p->com_bit));
    }
    return AG_OK;
}

/* ---------------------------------------------------------------- tone */

static uint8_t tone_active;

ag_status_t ag_hal_tone_start(const ag_pinref_t *p, uint16_t hz)
{
    uint32_t top;

    if (hz == 0u) {
        return ag_hal_tone_stop();
    }
    /* CTC on Timer2 with the pin toggling on compare: the output flips once per
     * match, so the wave is half the match rate. Prescaler 64 keeps the whole
     * audible range reachable with an 8-bit compare value. */
    top = (F_CPU / (2UL * 64UL * (uint32_t)hz)) - 1UL;
    if (top > 255UL) {
        return AG_ERR_RANGE;   /* below about 490 Hz at this prescaler */
    }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        DDRB |= (uint8_t)_BV(3);              /* OC2A is PB3 */
        TCCR2A = (uint8_t)(_BV(COM2A0) | _BV(WGM21));
        TCCR2B = (uint8_t)_BV(CS22);
        OCR2A  = (uint8_t)top;
    }
    (void)p;
    tone_active = 1u;
    return AG_OK;
}

ag_status_t ag_hal_tone_stop(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        TCCR2A = (uint8_t)(TCCR2A & (uint8_t)~_BV(COM2A0));
        TCCR2B = 0u;
    }
    tone_active = 0u;
    return AG_OK;
}
