/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "reset_cause.h"

ag_reset_cause_t ag_reset_cause_decode(uint8_t mcusr)
{
    if (mcusr & AG_MCUSR_WDRF)  { return AG_RESET_WATCHDOG; }
    if (mcusr & AG_MCUSR_BORF)  { return AG_RESET_BROWNOUT; }
    if (mcusr & AG_MCUSR_EXTRF) { return AG_RESET_EXTERNAL; }
    if (mcusr & AG_MCUSR_PORF)  { return AG_RESET_POWER_ON; }
    return AG_RESET_UNKNOWN;
}
