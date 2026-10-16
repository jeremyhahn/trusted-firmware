/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "hal_early.h"
#include "hal_reset_decode.h"
#include "../../core/handoff.h"

extern int  __heap_start;
extern int *__brkval;

/* The .init3 early init lives in hal_early.c, which the BOOTLOADER links too.
 * Sharing it rather than keeping a copy here is what makes "clear MCUSR and
 * disarm the watchdog" one sequence in the tree instead of two that can drift.
 * See that file for why those three instructions cannot happen later. */

tfw_status_t tfw_hal_init(void)
{
    /* The clock prescaler is written as a timed sequence: the enable bit is
     * only honoured within four cycles of being set. */
    CLKPR = (uint8_t)_BV(CLKPCE);
    CLKPR = 0u;                 /* full speed; no division */
    return TFW_OK;
}

tfw_reset_cause_t tfw_hal_reset_cause(void)
{
    uint8_t handed_off;

    /*
     * tfw-boot's handoff carries the (already decoded) cause IT saw, latched before ITS OWN
     * .init3 cleared the register -- issue #18. Preferred when a valid
     * handoff exists, because by the time this runs OUR OWN .init3 has
     * already read MCUSR a second time and found it clear: whatever ran
     * before us -- tfw-boot, or a stock bootloader with no handoff at all --
     * got there first. Falling back to our own latch (decoded inline: the application has no use for the
     * out-of-line copy in hal_early.c, which only tfw-boot links in) is exactly what a
     * stock bootloader's case needs: no handoff exists, MCUSR was already
     * zero by the time even OUR .init3 ran, and TFW_RESET_UNKNOWN is the
     * honest answer -- see docs/device-capabilities/.
     */
    if (tfw_boot_handoff_reset_cause(&handed_off)) {
        return (tfw_reset_cause_t)handed_off;
    }
    return tfw_avr_reset_decode(tfw_hal_early_flags);
}

/*
 * Reset via the watchdog rather than by jumping to address zero.
 *
 * A jump to zero re-enters the reset vector with every peripheral still
 * configured, every interrupt still enabled and the stack wherever it was. Half
 * the chip believes it is still running. The watchdog performs a real hardware
 * reset, which is what the caller asked for.
 *
 * Disarmed first, so the 15 ms period is a fresh start and not a prescaler
 * change on a watchdog already running. The trial (#77) calls this with the
 * 8 s watchdog armed; simavr keeps the running 8 s timer across a prescaler
 * change, so without the disarm the trial's deadline reset landed a whole
 * period late there.
 */
void tfw_hal_reset(void)
{
    cli();
    wdt_disable();
    wdt_enable(WDTO_15MS);
    for (;;) { }
}

void tfw_hal_irq_enable(void)
{
    sei();
}

void tfw_hal_delay_us(uint16_t us)
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
uint32_t tfw_hal_free_ram(void)
{
    int marker;
    int top = (__brkval == 0) ? (int)&__heap_start : (int)__brkval;
    /* int is 16 bits here, so the difference is already the whole answer; the
     * u32 return is the contract's width, for parts with more than 64 KiB. */
    return (uint16_t)((int)&marker - top);
}

void tfw_hal_watchdog_arm(void)
{
    wdt_enable(WDTO_8S);
}

void tfw_hal_watchdog_pet(void)
{
    wdt_reset();
}
