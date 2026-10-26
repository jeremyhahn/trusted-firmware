/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mps2 SoC's half of what both images share (src/hal/cortex-m/
 * hal_early.h): the CMSDK watchdog (the IoT Kit's Secure one on the AN505,
 * the APB one on the AN500). tfw-boot links this file as well
 * (mk/boot.mk), so disarm, arm and pet exist once.
 *
 * The watchdog counts LOAD down at the APB clock. At zero it raises its
 * interrupt (the NMI, which nothing here handles, so the core parks) and
 * reloads; at zero again with the interrupt still raised it resets the
 * system. A pet clears the interrupt and reloads the count. So an image that
 * stops petting is reset two periods after the last pet: 8 s with the
 * 4 s period below, AVR's longest watchdog period, which the trial's
 * deadline is written against (boot/boot_hal.h).
 *
 * Every write is bracketed by the lock: the registers ignore writes while it
 * is locked, which is how a stray store fails to disarm it.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "hal_early.h"
#include "soc_regs.h"

#define WDOG_PERIOD_S 4uL

_Static_assert((uint64_t)TFW_CM_CPU_HZ * WDOG_PERIOD_S <= 0xFFFFFFFFull,
               "the watchdog's LOAD is 32 bits");

void tfw_cm_soc_early(void)
{
    TFW_MPS2_WDOG_LOCK   = TFW_MPS2_WDOG_UNLOCK;
    TFW_MPS2_WDOG_CTRL   = 0u;
    TFW_MPS2_WDOG_INTCLR = 1u;
    TFW_MPS2_WDOG_LOCK   = 0u;
}

void tfw_cm_soc_watchdog_arm(void)
{
    tfw_cm_reset_mark    = TFW_CM_MARK_WATCHDOG;
    TFW_MPS2_WDOG_LOCK   = TFW_MPS2_WDOG_UNLOCK;
    TFW_MPS2_WDOG_LOAD   = (uint32_t)(TFW_CM_CPU_HZ * WDOG_PERIOD_S);
    TFW_MPS2_WDOG_INTCLR = 1u;
    TFW_MPS2_WDOG_CTRL   = TFW_MPS2_WDOG_CTRL_INTEN | TFW_MPS2_WDOG_CTRL_RESEN;
    TFW_MPS2_WDOG_LOCK   = 0u;
}

void tfw_cm_soc_watchdog_pet(void)
{
    TFW_MPS2_WDOG_LOCK   = TFW_MPS2_WDOG_UNLOCK;
    TFW_MPS2_WDOG_INTCLR = 1u;
    TFW_MPS2_WDOG_LOCK   = 0u;
}
