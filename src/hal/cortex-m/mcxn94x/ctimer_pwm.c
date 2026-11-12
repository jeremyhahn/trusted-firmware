/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <stdint.h>

#include "ctimer_pwm.h"

#define COUNTS (TFW_MCXN_CTIMER_PWM_MR3 + 1u)

tfw_status_t mcxn_ctimer_pwm_period(uint32_t clk_hz, uint32_t *pr, uint32_t *mr3)
{
    const uint32_t per_cycle = COUNTS * TFW_MCXN_CTIMER_PWM_HZ;
    uint32_t div;

    if (clk_hz < per_cycle) {
        return TFW_ERR_RANGE;
    }
    div = (uint32_t)(((uint64_t)clk_hz + per_cycle / 2u) / per_cycle);
    *pr = div - 1u;
    *mr3 = TFW_MCXN_CTIMER_PWM_MR3;
    return TFW_OK;
}
