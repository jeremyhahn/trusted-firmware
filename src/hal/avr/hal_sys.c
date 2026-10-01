/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "ag/hal.h"
#include "ag/config.h"
#include "hal_early.h"
#include "../../core/handoff.h"
#include "../../core/reset_cause.h"

extern int  __heap_start;
extern int *__brkval;

/* The .init3 early init lives in hal_early.c, which the BOOTLOADER links too.
 * Sharing it rather than keeping a copy here is what makes "clear MCUSR and
 * disarm the watchdog" one sequence in the tree instead of two that can drift.
 * See that file for why those three instructions cannot happen later. */

/* The bit positions ag_reset_cause_decode assumes, pinned against avr-libc --
 * the same arrangement each board's board_profile.h uses for register
 * addresses, so a transposed bit is a compile error naming the constant
 * rather than a silently wrong decode. */
_Static_assert(AG_MCUSR_PORF  == _BV(PORF),  "PORF bit position");
_Static_assert(AG_MCUSR_EXTRF == _BV(EXTRF), "EXTRF bit position");
_Static_assert(AG_MCUSR_BORF  == _BV(BORF),  "BORF bit position");
_Static_assert(AG_MCUSR_WDRF  == _BV(WDRF),  "WDRF bit position");

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
    uint8_t handed_off;

    /*
     * ag-boot's handoff carries the MCUSR IT saw, latched before ITS OWN
     * .init3 cleared the register -- issue #18. Preferred when a valid
     * handoff exists, because by the time this runs OUR OWN .init3 has
     * already read MCUSR a second time and found it clear: whatever ran
     * before us -- ag-boot, or a stock bootloader with no handoff at all --
     * got there first. Falling back to ag_hal_early_flags is exactly what a
     * stock bootloader's case needs: no handoff exists, MCUSR was already
     * zero by the time even OUR .init3 ran, and AG_RESET_UNKNOWN is the
     * honest answer -- see docs/device-capabilities/.
     */
    if (ag_boot_handoff_reset_cause(&handed_off)) {
        return ag_reset_cause_decode(handed_off);
    }
    return ag_reset_cause_decode(ag_hal_early_flags);
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
 *
 * Measured from __heap_start, not __bss_end: the linker places .noinit after
 * .bss, and .noinit holds live state (the reset-cause latch hal_early.c writes
 * in .init3), so __bss_end would count occupied bytes as free.
 */
uint16_t ag_hal_free_ram(void)
{
    int marker;
    int top = (__brkval == 0) ? (int)&__heap_start : (int)__brkval;
    return (uint16_t)((int)&marker - top);
}
