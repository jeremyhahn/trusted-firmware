/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

/* PWM on the i.MX 95 M7 through a TPM (tpm.h; SPEC row `imx95`, #22):
 * edge-aligned, polled, at TFW_IMX95_TPM_PWM_HZ, one 8-bit duty per channel.
 * The System Manager owns the pins' mux and the TPM's clock root
 * (board_profile.h), so neither is touched here. The TPM instance is
 * board_profile.h's TFW_IMX95_TPM_BASE; zero, while the instance the System
 * Manager gives the M7 is open, answers TFW_ERR_UNSUPPORTED. */

#include <stdint.h>

#include "tfw/hal.h"
#include "board_profile.h"
#include "cm_regs.h"
#include "tpm.h"

#define TPM_REG(base, off) TFW_CM_REG((base) + (off))

static tfw_status_t tpm_check(uintptr_t base, const tfw_pwmref_t *p)
{
    if (base == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (p->unit != 0u ||
        p->channel >= (TPM_REG(base, TFW_IMX95_TPM_PARAM) & TFW_IMX95_TPM_PARAM_CHAN)) {
        return TFW_ERR_RANGE;
    }
    return TFW_OK;
}

tfw_status_t tfw_imx95_tpm_write(uintptr_t base, uint32_t clk_hz,
                                 const tfw_pwmref_t *p, uint8_t duty)
{
    uint32_t     ps;
    uint32_t     mod;
    uint32_t     want;
    tfw_status_t st = tpm_check(base, p);

    if (st != TFW_OK) {
        return st;
    }
    tfw_imx95_tpm_period(clk_hz, (uint32_t)TFW_IMX95_TPM_PWM_HZ, &ps, &mod);
    want = TFW_IMX95_TPM_SC_CMOD_INC | ps;
    /* The counter is set up once, on the first channel: the period is the
     * same for every channel, so a later write leaves the others running.
     * PS and MOD are changed with the counter stopped ([DRV]). */
    if ((TPM_REG(base, TFW_IMX95_TPM_SC) &
         (TFW_IMX95_TPM_SC_CMOD | TFW_IMX95_TPM_SC_PS | TFW_IMX95_TPM_SC_CPWMS)) != want ||
        TPM_REG(base, TFW_IMX95_TPM_MOD) != mod) {
        TPM_REG(base, TFW_IMX95_TPM_SC)  = 0u;
        TPM_REG(base, TFW_IMX95_TPM_MOD) = mod;
        TPM_REG(base, TFW_IMX95_TPM_SC)  = want;
    }
    TPM_REG(base, TFW_IMX95_TPM_CnSC(p->channel)) =
        TFW_IMX95_TPM_CnSC_MSB | TFW_IMX95_TPM_CnSC_ELS_NORM;
    TPM_REG(base, TFW_IMX95_TPM_CnV(p->channel)) = tfw_imx95_tpm_cnv(mod, duty);
    return TFW_OK;
}

tfw_status_t tfw_imx95_tpm_stop(uintptr_t base, const tfw_pwmref_t *p)
{
    tfw_status_t st = tpm_check(base, p);

    if (st != TFW_OK) {
        return st;
    }
    /* MSB, MSA and ELS all zero disables the channel ([DRV]); the counter
     * keeps running for the other channels. */
    TPM_REG(base, TFW_IMX95_TPM_CnSC(p->channel)) = 0u;
    TPM_REG(base, TFW_IMX95_TPM_CnV(p->channel))  = 0u;
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    return tfw_imx95_tpm_write((uintptr_t)TFW_IMX95_TPM_BASE,
                               (uint32_t)TFW_IMX95_TPM_CLK_HZ, p, duty);
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    return tfw_imx95_tpm_stop((uintptr_t)TFW_IMX95_TPM_BASE, p);
}
