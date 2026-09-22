/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "ag/hal.h"
#include "ag/config.h"

static volatile ag_ms_t ticks;

/*
 * A TRUE millisecond, not Arduino's 1.024 ms.
 *
 * Timer0 in CTC with a prescaler of 64 counts 250 ticks of 4 us at 16 MHz,
 * which is exactly 1.000 ms. Arduino's millis() counts timer overflows and
 * accumulates 2.4% error, so a valve told to run for 600 seconds runs for 614.
 * For a firmware whose whole job includes timed outputs, that is not a rounding
 * detail.
 *
 * The cost is Timer0's two output-compare pins, which the board descriptor
 * marks AG_PWM_RSVD so CAPS tells a host up front.
 */
#define TICK_OCR ((uint8_t)((F_CPU / 64UL / 1000UL) - 1UL))

ag_status_t ag_hal_tick_init(void)
{
    TCCR0A = (uint8_t)_BV(WGM01);                 /* CTC */
    TCCR0B = (uint8_t)(_BV(CS01) | _BV(CS00));    /* clk/64 */
    OCR0A  = TICK_OCR;
    TIMSK0 = (uint8_t)_BV(OCIE0A);
    return AG_OK;
}

/* The whole interrupt. Ten cycles, and deliberately nothing else: the scheduler
 * sweep runs in the main loop instead, so g_slots has exactly one writer and
 * needs no locking, and a long ISR here would perturb the bit timing of a
 * software UART on the parts that need one. */
ISR(TIMER0_COMPA_vect)
{
    ticks++;
}

/*
 * THE ONLY ATOMIC SECTION IN THE FIRMWARE.
 *
 * ticks is four bytes and the ISR writes it, so an unguarded read can catch a
 * carry half-propagated -- returning a value that was never true, once every
 * 256 milliseconds, which is frequent enough to matter and rare enough to be
 * blamed on something else for a week.
 *
 * It lives HERE, inside the HAL, which is what lets src/caps/sched.c be plain
 * portable C with no volatile, no <util/atomic.h>, and no knowledge that
 * interrupts exist -- and therefore compile and run on the host.
 */
ag_ms_t ag_hal_millis(void)
{
    ag_ms_t t;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        t = ticks;
    }
    return t;
}
