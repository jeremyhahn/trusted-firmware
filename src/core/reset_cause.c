/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "reset_cause.h"

tfw_reset_cause_t tfw_reset_cause_decode(uint8_t mcusr)
{
    if (mcusr & TFW_MCUSR_WDRF)  { return TFW_RESET_WATCHDOG; }
    if (mcusr & TFW_MCUSR_BORF)  { return TFW_RESET_BROWNOUT; }
    if (mcusr & TFW_MCUSR_EXTRF) { return TFW_RESET_EXTERNAL; }
    if (mcusr & TFW_MCUSR_PORF)  { return TFW_RESET_POWER_ON; }
    return TFW_RESET_UNKNOWN;
}
