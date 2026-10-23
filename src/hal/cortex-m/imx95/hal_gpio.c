/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Digital pins on the i.MX 95's RGPIO banks (soc_regs.h, soc_calc.c; TO
 * CONFIRM against NXP IMX95RM, the RGPIO chapter). Port n of the board's
 * table (boards/frdm-imx95-m7/board.c) is GPIOn+1, bit n line n.
 *
 * Direction is the bank's PDDR. A write goes through PSOR or PCOR, which set
 * or clear one line in a single store, so no read-modify-write of the output
 * register races the A55 or another core driving a line of the same bank.
 * A read is PDIR, the pad's level, for an input and an output alike.
 *
 * The pull-up is the pad's, in the IOMUXC, which the System Manager owns: the
 * M7 cannot set it, so TFW_PIN_INPUT_PULLUP is TFW_ERR_UNSUPPORTED rather
 * than accepted as a plain input. A coordinate past the SoC's banks or lines
 * is TFW_ERR_BAD_PIN.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"

#define GPIO_LINES 32u

static uint32_t bank_of(const tfw_pinref_t *pin)
{
    if (pin->bit >= GPIO_LINES) {
        return 0u;
    }
    return tfw_imx95_gpio_base(pin->port);
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *pin, tfw_pin_mode_t mode)
{
    uint32_t base = bank_of(pin);
    uint32_t bit;
    uint32_t primask;

    if (base == 0u) {
        return TFW_ERR_BAD_PIN;
    }
    if (mode != TFW_PIN_INPUT && mode != TFW_PIN_OUTPUT) {
        return TFW_ERR_UNSUPPORTED;
    }
    bit     = 1u << pin->bit;
    primask = tfw_cm_irq_save();
    if (mode == TFW_PIN_OUTPUT) {
        TFW_IMX95_GPIO_REG(base, TFW_IMX95_GPIO_PDDR) |= bit;
    } else {
        TFW_IMX95_GPIO_REG(base, TFW_IMX95_GPIO_PDDR) &= ~bit;
    }
    tfw_cm_irq_restore(primask);
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *pin, uint8_t level)
{
    uint32_t base = bank_of(pin);

    if (base == 0u) {
        return TFW_ERR_BAD_PIN;
    }
    TFW_IMX95_GPIO_REG(base, level ? TFW_IMX95_GPIO_PSOR : TFW_IMX95_GPIO_PCOR) =
        1u << pin->bit;
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *pin, uint8_t *level)
{
    uint32_t base = bank_of(pin);

    if (base == 0u) {
        return TFW_ERR_BAD_PIN;
    }
    *level = (uint8_t)((TFW_IMX95_GPIO_REG(base, TFW_IMX95_GPIO_PDIR) >> pin->bit) & 1u);
    return TFW_OK;
}
