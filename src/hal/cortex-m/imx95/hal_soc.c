/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 SoC's half of what both images share (src/hal/cortex-m/
 * hal_early.h): WDOG5, a WDOG32 (soc_regs.h; TO CONFIRM against NXP IMX95RM,
 * the WDOG chapter, and against the System Manager configuration that gives
 * the watchdog to the M7).
 *
 * The watchdog counts up from the 32 kHz LPO divided by 256 and resets the
 * M7 at TOVAL, the first timeout: there is no warning period as on the CMSDK
 * watchdog. So TOVAL is the 8 s after the last pet at which mps2-tz resets,
 * two of its 4 s periods, AVR's longest watchdog period, which the trial's
 * deadline is written against (boot/boot_hal.h).
 *
 * Each configuration is one unlock, then the writes, inside the window the
 * block allows after it, with interrupts off so nothing stretches it. CS keeps
 * UPDATE set so a later configuration is allowed at all: a WDOG32 written
 * without it is fixed until reset. A refresh is one 32-bit store of the
 * refresh key (CS.CMD32EN).
 */

#include <stdint.h>

#include "tfw/config.h"
#include "hal_early.h"
#include "soc_regs.h"

#define WDOG_TIMEOUT_S 8u
#define WDOG_TOVAL     ((TFW_IMX95_WDOG_LPO_HZ / 256u) * WDOG_TIMEOUT_S)

/* A constant, not soc_calc.c's arithmetic: tfw-boot links this file without
 * the rest of the SoC directory (mk/boot.mk's BOOT_SOC_SRCS). */
_Static_assert(WDOG_TOVAL > 0u && WDOG_TOVAL <= 0xFFFFu,
               "the watchdog's timeout is TOVAL's 16 bits");

#define WDOG_CS_BASE (TFW_IMX95_WDOG_CS_UPDATE | TFW_IMX95_WDOG_CS_CLK_LPO | \
                      TFW_IMX95_WDOG_CS_PRES | TFW_IMX95_WDOG_CS_CMD32EN | \
                      TFW_IMX95_WDOG_CS_FLG)

/* A bound on each status wait, so a watchdog the System Manager left locked
 * (UPDATE clear) cannot park the core here: the unlock is then ignored and
 * the configuration with it, which the next reset makes visible. */
#define WDOG_WAIT 1000u

static void wdog_configure(uint32_t cs, uint32_t toval)
{
    uint32_t primask = tfw_cm_irq_save();
    uint32_t n;

    TFW_IMX95_WDOG_CNT = TFW_IMX95_WDOG_UNLOCK;
    for (n = 0u; n < WDOG_WAIT && (TFW_IMX95_WDOG_CS & TFW_IMX95_WDOG_CS_ULK) == 0u; n++) {
    }
    TFW_IMX95_WDOG_TOVAL = toval;
    TFW_IMX95_WDOG_CS    = cs;
    for (n = 0u; n < WDOG_WAIT && (TFW_IMX95_WDOG_CS & TFW_IMX95_WDOG_CS_RCS) == 0u; n++) {
    }
    tfw_cm_irq_restore(primask);
}

void tfw_cm_soc_early(void)
{
    wdog_configure(WDOG_CS_BASE, 0xFFFFu);
}

void tfw_cm_soc_watchdog_arm(void)
{
    tfw_cm_reset_mark = TFW_CM_MARK_WATCHDOG;
    wdog_configure(WDOG_CS_BASE | TFW_IMX95_WDOG_CS_EN,
                   WDOG_TOVAL);
}

void tfw_cm_soc_watchdog_pet(void)
{
    TFW_IMX95_WDOG_CNT = TFW_IMX95_WDOG_REFRESH;
}
