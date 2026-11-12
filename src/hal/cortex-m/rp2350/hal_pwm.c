/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * RP2350 PWM: 8-bit duty at 1 kHz on one of the twelve slices.
 *
 * A pwm ref names the slice (unit), the channel (0 = A, 1 = B) and the GPIO
 * (port 0, bit n); the slice and channel must be the ones the GPIO is wired to
 * ([RP-DS] release 8 §12.5.2), so a table row that disagrees is refused.
 *
 * Nothing is set up at boot. The first write takes PWM out of reset, and the
 * first write to a slice sets its divider (tfw_rp_pwm_div, soc_calc.h) and
 * TOP 255 and enables it free-running; the first write to a pin selects the
 * PWM function on it. CC = duty except that 255 is TOP + 1, so 0 never goes
 * high and 255 never goes low. Stop disables the slice, which stops both of
 * its channels, and gives the pin back to SIO driven low.
 */
#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_calc.h"

#define PORT_GPIO  0u
#define GPIO_COUNT 30u

static uint8_t  pwm_out_of_reset;
static uint16_t slice_on;   /* bit n: slice n configured and enabled */
static uint32_t pin_on;     /* bit n: GPIO n's FUNCSEL is PWM */

static uint8_t pwm_ref_ok(const tfw_pwmref_t *p)
{
    return p->port == PORT_GPIO && p->bit < GPIO_COUNT &&
           p->unit < TFW_RP_PWM_SLICES &&
           p->unit == ((p->bit >> 1) & 7u) && p->channel == (p->bit & 1u);
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    uint32_t cc, shift;

    if (!pwm_ref_ok(p)) {
        return TFW_ERR_BAD_PIN;
    }
    if (!(slice_on & (1u << p->unit))) {
        uint16_t div = tfw_rp_pwm_div((uint32_t)TFW_CM_CPU_HZ);

        if (div == 0u) {
            return TFW_ERR_INTERNAL;
        }
        if (!pwm_out_of_reset) {
            TFW_RP_CLR(TFW_RP_RESETS_RESET) = TFW_RP_RESET_PWM;
            while ((TFW_RP_RESETS_DONE & TFW_RP_RESET_PWM) == 0u) {
            }
            pwm_out_of_reset = 1u;
        }
        TFW_CM_REG(TFW_RP_PWM_CSR(p->unit)) = 0u;
        TFW_CM_REG(TFW_RP_PWM_DIV(p->unit)) = div;
        TFW_CM_REG(TFW_RP_PWM_TOP(p->unit)) = TFW_RP_PWM_TOP_8BIT;
        TFW_CM_REG(TFW_RP_PWM_CTR(p->unit)) = 0u;
        TFW_CM_REG(TFW_RP_PWM_CSR(p->unit)) = TFW_RP_PWM_CSR_EN;
        slice_on |= (uint16_t)(1u << p->unit);
    }

    shift = p->channel ? TFW_RP_PWM_CC_B_SHIFT : 0u;
    cc    = TFW_CM_REG(TFW_RP_PWM_CC(p->unit)) & ~(0xFFFFu << shift);
    cc   |= (duty == 255u ? TFW_RP_PWM_TOP_8BIT + 1u : (uint32_t)duty) << shift;
    TFW_CM_REG(TFW_RP_PWM_CC(p->unit)) = cc;

    if (!(pin_on & ((uint32_t)1u << p->bit))) {
        TFW_CM_REG(TFW_RP_PADS_GPIO(p->bit)) = tfw_rp_pad_value(0u);
        TFW_RP_IO_CTRL(p->bit) = TFW_RP_FUNC_PWM;
        pin_on |= (uint32_t)1u << p->bit;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    uint32_t mask;

    if (!pwm_ref_ok(p)) {
        return TFW_ERR_BAD_PIN;
    }
    mask = (uint32_t)1u << p->bit;
    if (slice_on & (1u << p->unit)) {
        TFW_RP_CLR(TFW_RP_PWM_CSR(p->unit)) = TFW_RP_PWM_CSR_EN;
        slice_on &= (uint16_t)~(1u << p->unit);
    }
    TFW_RP_SIO_GPIO_OUT_CLR = mask;
    TFW_RP_SIO_GPIO_OE_SET  = mask;
    TFW_RP_IO_CTRL(p->bit)  = TFW_RP_FUNC_SIO;
    pin_on &= ~mask;
    return TFW_OK;
}
