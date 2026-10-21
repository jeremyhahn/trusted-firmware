/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Recovery by stopping: linked when BOOT_OTA=0, as boot/hal/avr/recovery_halt.c
 * is and for its reasons. No image may run and this row has no way to accept
 * one, so the core stops, with interrupts masked, until someone loads an image.
 */

#include "cm_regs.h"
#include "../../boot_recovery.h"

/* No loop to open a session in: the request is spent by the caller and this
 * returns at once, so the device boots its image. */
void tfw_boot_session_open(tfw_boot_platform_t *plat,
                          const tfw_boot_session_limits_t *lim)
{
    (void)plat;
    (void)lim;
}

_Noreturn void tfw_boot_recovery(tfw_boot_platform_t *plat, tfw_status_t reason)
{
    (void)plat;
    (void)reason;

    /* No interrupt is ever enabled in tfw-boot; masking them here says so at
     * the point it matters. WFI with PRIMASK set still sleeps until a pending
     * interrupt, and none is enabled, so the core sleeps rather than spins. */
    tfw_cm_irq_off();
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
