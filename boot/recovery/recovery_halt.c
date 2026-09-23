/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Recovery by stopping: linked when BOOT_OTA=0.
 *
 * On a part whose boot section is 4 KB, the framed OTA path plus the hash plus
 * the boot decision do not fit together -- measured, in docs/security/bootloader.md.
 * Something has to go, and the update path is the only piece that has somewhere
 * else it can live: an application can receive an image, stage it and reboot,
 * and this bootloader then measures, verifies and commits it. What is lost is
 * the case where there is no working application to do the receiving.
 *
 * So this is not a stub and not a reduced OTA. It is the honest behaviour when
 * no image may run and there is no way to accept one: stop, with interrupts
 * off, and wait for a programmer. A bootloader that jumped anyway, or that
 * timed out into the unverified image, would be a verifier that gives up --
 * which is worse than one that is absent, because it looks like one that works.
 */

#include <avr/interrupt.h>
#include <avr/sleep.h>

#include "../boot_recovery.h"

void ag_boot_recovery(ag_boot_platform_t *plat, ag_status_t reason)
{
    (void)plat;
    (void)reason;

    /* Interrupts are already off -- none are ever enabled in the bootloader --
     * but cli() here says so at the point it matters rather than relying on a
     * property established three files away. */
    cli();
    for (;;) {
        /* Power-down rather than a spin. A device waiting for a programmer may
         * wait a long time, and a bootloader that burns current doing nothing
         * is a battery-powered device that is flat when someone arrives. */
        /* set_sleep_mode() expands to a masked assignment whose constant is
         * negative in int, which -Wconversion rejects. Writing SMCR directly
         * says the same thing without asking the warning set to be relaxed for
         * a header we did not write. */
        SMCR = (uint8_t)((SMCR & 0xF1u) | (uint8_t)(SLEEP_MODE_PWR_DOWN & 0x0Eu));
        sleep_mode();
    }
}
