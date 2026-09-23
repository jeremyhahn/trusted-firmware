/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE EARLIEST CODE THAT RUNS -- in BOTH images.
 *
 * Its own translation unit because the application and the bootloader each need
 * it and it must be the same three instructions in each. Two copies would be
 * two chances to get wrong the one sequence whose failure mode is a part that
 * bootloops forever and looks dead -- and the copy that is wrong would be the
 * one in the image that is harder to replace.
 *
 * .init3 executes before the C runtime has zeroed .bss and long before main().
 * Two things must happen here and cannot happen later.
 *
 * MCUSR must be READ AND CLEARED. It is the only record of why the part
 * restarted, and it is not cleared by reset -- the flags accumulate. A build
 * that reads it in main() sees a value polluted by every previous restart.
 *
 * The WATCHDOG MUST BE DISARMED. On every classic AVR a watchdog reset leaves
 * WDE set and the prescaler at its shortest interval, so the part resets again
 * before it can reach main() and disable it. The result is a board that
 * bootloops forever and looks dead -- and the only way out is a power cycle
 * plus an ISP, which on a soldered-down device means a trip with a
 * screwdriver. This is the single most common way to brick an AVR in the field,
 * and three instructions here prevent it.
 *
 * IT MATTERS MORE IN THE BOOTLOADER, not less. With BOOTRST programmed the part
 * enters the boot section on every reset, so this is now the first code after
 * EVERY watchdog bite -- including the one the application's own ag_hal_reset()
 * deliberately causes. A bootloader that did not disarm the watchdog would take
 * the reset it was asked for and never get far enough to hand control back.
 *
 * `used` and `naked` because nothing calls this: the linker places it, and a
 * prologue would be wasted.
 */

#include <avr/io.h>
#include <avr/wdt.h>

#include "hal_early.h"

uint8_t ag_hal_early_flags;

void ag_hal_early_init(void) __attribute__((naked, used, section(".init3")));
void ag_hal_early_init(void)
{
    ag_hal_early_flags = MCUSR;
    MCUSR              = 0u;
    wdt_disable();
}
