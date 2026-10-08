/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "tfw/hal.h"
#include "tfw/config.h"

static volatile tfw_ms_t ticks;

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
 * marks TFW_PWM_RSVD so CAPS tells a host up front.
 */
#define TICK_OCR ((uint8_t)((F_CPU / 64UL / 1000UL) - 1UL))

/*
 * The name of the register holding OCIE0A, and the only difference in the whole
 * tick path across this family.
 *
 * Every mega part has one interrupt-mask register per timer and calls this one
 * TIMSK0. The ATtiny85 has ONE mask register for both its timers and calls it
 * TIMSK. The timer, the waveform mode, the prescaler and the vector name are
 * identical, so this is a spelling and it is isolated here -- src/hal/avr being
 * one of the four places a preprocessor conditional is permitted, and a
 * register with two names on the same architecture being exactly what that
 * exemption is for.
 */
#ifdef TIMSK0
#define TFW_TICK_TIMSK TIMSK0
#else
#define TFW_TICK_TIMSK TIMSK
#endif

tfw_status_t tfw_hal_tick_init(void)
{
    TCCR0A = (uint8_t)_BV(WGM01);                 /* CTC */
    TCCR0B = (uint8_t)(_BV(CS01) | _BV(CS00));    /* clk/64 */
    OCR0A  = TICK_OCR;
    /* OR, not assign. On a part whose mask register is shared -- the ATtiny85's
     * TIMSK also carries OCIE1A and OCIE1B, which on that part are the software
     * UART's bit clock -- an assignment here silently disables whichever of
     * them was already armed. The register reads zero at reset, so this is
     * identical on every board that does not share it. */
    TFW_TICK_TIMSK |= (uint8_t)_BV(OCIE0A);
    return TFW_OK;
}

/* The whole interrupt. Measured on the shipped image in a cycle-accurate
 * emulator: 58 cycles on the 328P, 59 on the 2560, both including the part's
 * four-cycle interrupt response -- not ten. `ticks` is an tfw_ms_t, four bytes
 * and volatile: the increment itself is four `lds`, an `adiw`, two `adc` and
 * four `sts`, twenty cycles, and to do that the compiler must save r24-r27
 * plus r1 and SREG around it, nineteen more in push and pop. Twenty-seven
 * instructions from the one line of source below. Deliberately nothing else:
 * the scheduler sweep runs in the main loop instead, so g_slots has exactly
 * one writer and needs no locking, and this cost -- not a shorter one -- is
 * what must not perturb the bit timing of a software UART on the parts that
 * need one. */
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
tfw_ms_t tfw_hal_millis(void)
{
    tfw_ms_t t;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        t = ticks;
    }
    return t;
}
