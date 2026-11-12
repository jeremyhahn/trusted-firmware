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

uint32_t mcxn_ctimer_pwm_match(uint8_t duty)
{
    return COUNTS - (uint32_t)duty;
}

tfw_status_t mcxn_ctimer_pwm_start(volatile uint32_t *ct, uint32_t clk_hz)
{
    uint32_t pr;
    uint32_t mr3;
    tfw_status_t st = mcxn_ctimer_pwm_period(clk_hz, &pr, &mr3);

    if (st != TFW_OK) {
        return st;
    }
    ct[TFW_MCXN_CT_TCR] = TFW_MCXN_CT_TCR_CRST;
    ct[TFW_MCXN_CT_CTCR] = 0u;                 /* timer mode: count the clock */
    ct[TFW_MCXN_CT_PWMC] = 0u;
    ct[TFW_MCXN_CT_PR] = pr;
    for (uint32_t n = 0u; n < TFW_MCXN_CTIMER_PWM_CHANNELS; n++) {
        ct[TFW_MCXN_CT_MR(n)] = mcxn_ctimer_pwm_match(0u);
    }
    ct[TFW_MCXN_CT_MR(3u)] = mr3;
    ct[TFW_MCXN_CT_MCR] = TFW_MCXN_CT_MCR_MR3R;  /* the period: reset on MR3 */
    ct[TFW_MCXN_CT_TCR] = TFW_MCXN_CT_TCR_CEN;
    return TFW_OK;
}

tfw_status_t mcxn_ctimer_pwm_duty(volatile uint32_t *ct, uint8_t channel, uint8_t duty)
{
    if (channel >= TFW_MCXN_CTIMER_PWM_CHANNELS) {
        return TFW_ERR_RANGE;
    }
    ct[TFW_MCXN_CT_MR(channel)] = mcxn_ctimer_pwm_match(duty);
    ct[TFW_MCXN_CT_PWMC] |= 1u << channel;
    return TFW_OK;
}

uint8_t mcxn_ctimer_pwm_halt(volatile uint32_t *ct, uint8_t channel)
{
    if (channel < TFW_MCXN_CTIMER_PWM_CHANNELS) {
        ct[TFW_MCXN_CT_PWMC] &= ~(1u << channel);
        ct[TFW_MCXN_CT_MR(channel)] = mcxn_ctimer_pwm_match(0u);
    }
    if (ct[TFW_MCXN_CT_PWMC] != 0u) {
        return 0u;
    }
    ct[TFW_MCXN_CT_TCR] = TFW_MCXN_CT_TCR_CRST;
    return 1u;
}
