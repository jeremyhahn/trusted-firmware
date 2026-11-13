/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

/* The i.MX 95 Timer/PWM Module (TPM), as the M7 drives it for PWM (SPEC row
 * `imx95`, #22). Kept apart from soc_regs.h so the port's other peripheral
 * lanes do not edit the same lines.
 *
 * Sources (docs/reference/README.md), read for facts only, no code copied:
 *  [DRV]  Linux v6.12 drivers/pwm/pwm-imx-tpm.c: the register offsets, the
 *         PARAM, SC and CnSC fields, the 16-bit MOD, the prescaler search and
 *         MOD = period count - 1.
 *  [DTSI] Linux v6.12 arch/arm64/boot/dts/freescale/imx95.dtsi: the six TPM
 *         instances' bases (below, for reference; which one the System
 *         Manager gives the M7 is open, imx95-design.md question 17, so the
 *         base the HAL uses is board_profile.h's TFW_IMX95_TPM_BASE).
 * IMX95RM is not held: every value here is TO CONFIRM against it. */

#ifndef TFW_IMX95_TPM_H
#define TFW_IMX95_TPM_H

#include <stdint.h>

#include "tfw/hal.h"

/* Instance bases [DTSI]: tpm1 and tpm2 in the low-power (AON) domain, tpm3
 * to tpm6 in the wakeup domain. Not used by the HAL. */
#define TFW_IMX95_TPM1_BASE 0x44310000u
#define TFW_IMX95_TPM2_BASE 0x44320000u
#define TFW_IMX95_TPM3_BASE 0x424E0000u
#define TFW_IMX95_TPM4_BASE 0x424F0000u
#define TFW_IMX95_TPM5_BASE 0x42500000u
#define TFW_IMX95_TPM6_BASE 0x42510000u

/* Register offsets [DRV]. */
#define TFW_IMX95_TPM_PARAM     0x04u
#define TFW_IMX95_TPM_GLOBAL    0x08u
#define TFW_IMX95_TPM_SC        0x10u
#define TFW_IMX95_TPM_CNT       0x14u
#define TFW_IMX95_TPM_MOD       0x18u
#define TFW_IMX95_TPM_CnSC(n)   (0x20u + 8u * (uint32_t)(n))
#define TFW_IMX95_TPM_CnV(n)    (0x24u + 8u * (uint32_t)(n))
/* STATUS and CONF: not in [DRV]; NXP's TPM layout as the MCUXpresso SDK
 * names it, TO CONFIRM against IMX95RM. The HAL never touches either; the
 * host test's model checks that it does not. */
#define TFW_IMX95_TPM_STATUS    0x1Cu
#define TFW_IMX95_TPM_CONF      0x84u

/* PARAM [DRV]: the channel count, bits 7:0. */
#define TFW_IMX95_TPM_PARAM_CHAN    0xFFu
/* SC [DRV]: prescale PS (2:0, divide by 2^PS), clock mode CMOD (4:3, 1 =
 * count every TPM clock), CPWMS (5, 0 = up counting, edge-aligned). */
#define TFW_IMX95_TPM_SC_PS         0x07u
#define TFW_IMX95_TPM_SC_CMOD       0x18u
#define TFW_IMX95_TPM_SC_CMOD_INC   0x08u
#define TFW_IMX95_TPM_SC_CPWMS      0x20u
/* CnSC [DRV]: MSB (5) with ELS = 2 (3:2) is edge-aligned PWM, high-true:
 * the output is set at the counter's overflow and cleared at CnV. All zero
 * is the channel disabled. */
#define TFW_IMX95_TPM_CnSC_MSB      0x20u
#define TFW_IMX95_TPM_CnSC_MSA      0x10u
#define TFW_IMX95_TPM_CnSC_ELS      0x0Cu
#define TFW_IMX95_TPM_CnSC_ELS_NORM 0x08u
/* MOD is 16 bits wide [DRV]. */
#define TFW_IMX95_TPM_MOD_MAX       0xFFFFu
#define TFW_IMX95_TPM_PS_MAX        7u

/* The PWM frequency, as src/hal/esp32/hal_pwm.c's PWM_HZ: the wire carries
 * an 8-bit duty, not a frequency. */
#define TFW_IMX95_TPM_PWM_HZ        1000uL

/* The prescaler and MOD for a PWM of freq_hz from a TPM clock of clk_hz:
 * the smallest PS whose period count fits MOD's 16 bits, then MOD = count -
 * 1 ([DRV] pwm_imx_tpm_round_state). A period too long for PS 7 is clamped
 * to the longest; a clock slower than the frequency gives MOD 0. */
static inline void tfw_imx95_tpm_period(uint32_t clk_hz, uint32_t freq_hz,
                                        uint32_t *ps, uint32_t *mod)
{
    uint32_t ticks = (freq_hz == 0u) ? 0u : clk_hz / freq_hz;
    uint32_t p     = 0u;

    while ((ticks >> p) > TFW_IMX95_TPM_MOD_MAX + 1u && p < TFW_IMX95_TPM_PS_MAX) {
        p++;
    }
    ticks >>= p;
    if (ticks > TFW_IMX95_TPM_MOD_MAX + 1u) {
        ticks = TFW_IMX95_TPM_MOD_MAX + 1u;
    }
    *ps  = p;
    *mod = (ticks == 0u) ? 0u : ticks - 1u;
}

/* CnV for an 8-bit duty over a period of MOD + 1 counts. Duty 0 is CnV 0,
 * the output never set (low); duty 255 is MOD + 1, past MOD, so the output
 * is never cleared (high). The product fits: 255 * 0x10000 < 2^32. */
static inline uint32_t tfw_imx95_tpm_cnv(uint32_t mod, uint8_t duty)
{
    return ((uint32_t)duty * (mod + 1u)) / 255u;
}

/* The HAL's PWM on the TPM at base, so a test can drive a model at a base
 * of its own and the board's zero. Base zero answers TFW_ERR_UNSUPPORTED and
 * touches no register; a unit other than 0 or a channel past PARAM's count
 * answers TFW_ERR_RANGE. */
tfw_status_t tfw_imx95_tpm_write(uintptr_t base, uint32_t clk_hz,
                                 const tfw_pwmref_t *p, uint8_t duty);
tfw_status_t tfw_imx95_tpm_stop(uintptr_t base, const tfw_pwmref_t *p);

#endif /* TFW_IMX95_TPM_H */
