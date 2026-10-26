/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stm32h5 SoC's reset cause, from RCC_RSR (offset 0xF4, stm32h573xx.h
 * line 1211, from RCC_BASE_NS 0x44020C00, line 1970; cmsis-device-h5,
 * docs/reference/README.md names the revision). The flags are
 * stm32h573xx.h's RCC_RSR_*_Pos:
 *
 *   bit  field     line   cause
 *   23   RMVF      17417  not a cause: written 1, it clears every flag
 *   26   PINRSTF   17420  TFW_RESET_EXTERNAL when alone: the NRST pin
 *   27   BORRSTF   17423  TFW_RESET_POWER_ON: the brownout reset, which a
 *                         power-up is too
 *   28   SFTRSTF   17426  TFW_RESET_SOFTWARE when the firmware asked:
 *                         AIRCR.SYSRESETREQ, which tfw_cm_system_reset
 *                         writes after its marker; else unknown (a
 *                         debugger can ask too)
 *   29   IWDGRSTF  17429  TFW_RESET_WATCHDOG: the IWDG hal_soc.c arms
 *   30   WWDGRSTF  17432  TFW_RESET_WATCHDOG
 *   31   LPWRRSTF  17435  unknown: the low-power reset
 *   0-22, 24-25    -      unknown: the header defines none
 *
 * The header names the flags, not when each is set. RM0481's RCC chapter
 * says that, and it was not retrieved (docs/reference/README.md): whether an
 * internal reset, which drives NRST low, also latches PINRSTF is to confirm
 * there. So PINRSTF beside one other cause is that cause, and PINRSTF alone
 * is the pin. A brownout and a power-up both set BORRSTF, so a brownout reads
 * as power-on. No flag at all is unknown, not power-on: BORRSTF is the one a
 * power-up sets.
 *
 * The flags are cleared by writing RMVF (RCC_RSR_RMVF_Msk, line 17418); that
 * a write of it clears them is RM0481's to confirm too.
 */

#include <stdint.h>

#include "soc_reset.h"

#define RSR_RMVF     (1u << 23)
#define RSR_PINRSTF  (1u << 26)
#define RSR_BORRSTF  (1u << 27)
#define RSR_SFTRSTF  (1u << 28)
#define RSR_IWDGRSTF (1u << 29)
#define RSR_WWDGRSTF (1u << 30)

tfw_reset_cause_t tfw_h5_reset_decode(uint32_t rsr, uint8_t requested)
{
    const uint32_t cause = rsr & ~RSR_PINRSTF;

    if (cause == 0u) {
        return rsr == RSR_PINRSTF ? TFW_RESET_EXTERNAL : TFW_RESET_UNKNOWN;
    }
    if (cause == RSR_BORRSTF) {
        return TFW_RESET_POWER_ON;
    }
    if ((cause & ~(RSR_IWDGRSTF | RSR_WWDGRSTF)) == 0u) {
        return TFW_RESET_WATCHDOG;
    }
    if (cause == RSR_SFTRSTF && requested != 0u) {
        return TFW_RESET_SOFTWARE;
    }
    return TFW_RESET_UNKNOWN;
}

tfw_reset_cause_t tfw_h5_reset_take(volatile uint32_t *rsr, uint8_t requested)
{
    const uint32_t latched = *rsr;

    *rsr = RSR_RMVF;
    return tfw_h5_reset_decode(latched, requested);
}
