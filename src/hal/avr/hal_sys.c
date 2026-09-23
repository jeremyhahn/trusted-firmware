/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "ag/hal.h"
#include "ag/config.h"
#include "hal_early.h"

extern int  __bss_end;
extern int *__brkval;

/* The .init3 early init lives in hal_early.c, which the BOOTLOADER links too.
 * Sharing it rather than keeping a copy here is what makes "clear MCUSR and
 * disarm the watchdog" one sequence in the tree instead of two that can drift.
 * See that file for why those three instructions cannot happen later. */

ag_status_t ag_hal_init(void)
{
    /* The clock prescaler is written as a timed sequence: the enable bit is
     * only honoured within four cycles of being set. */
    CLKPR = (uint8_t)_BV(CLKPCE);
    CLKPR = 0u;                 /* full speed; no division */
    return AG_OK;
}

ag_reset_cause_t ag_hal_reset_cause(void)
{
    /* Checked most-specific first. A brownout during a watchdog reset sets
     * both, and the brownout is the more useful thing to report -- it names a
     * power problem rather than a software one. */
    if (ag_hal_early_flags & _BV(WDRF))  { return AG_RESET_WATCHDOG; }
    if (ag_hal_early_flags & _BV(BORF))  { return AG_RESET_BROWNOUT; }
    if (ag_hal_early_flags & _BV(EXTRF)) { return AG_RESET_EXTERNAL; }
    if (ag_hal_early_flags & _BV(PORF))  { return AG_RESET_POWER_ON; }
    return AG_RESET_UNKNOWN;
}

/*
 * Reset via the watchdog rather than by jumping to address zero.
 *
 * A jump to zero re-enters the reset vector with every peripheral still
 * configured, every interrupt still enabled and the stack wherever it was. Half
 * the chip believes it is still running. The watchdog performs a real hardware
 * reset, which is what the caller asked for.
 */
void ag_hal_reset(void)
{
    cli();
    wdt_enable(WDTO_15MS);
    for (;;) { }
}

void ag_hal_delay_us(uint16_t us)
{
    while (us--) {
        _delay_us(1);
    }
}

/*
 * Free SRAM: the gap between the top of the heap and the current stack.
 *
 * There is no allocator in this firmware, so __brkval is normally zero and the
 * measurement is stack headroom. Reported by SYS_INFO because a host watching
 * it fall knows about a problem before the device dies of one.
 */
uint16_t ag_hal_free_ram(void)
{
    int marker;
    int top = (__brkval == 0) ? (int)&__bss_end : (int)__brkval;
    return (uint16_t)((int)&marker - top);
}
