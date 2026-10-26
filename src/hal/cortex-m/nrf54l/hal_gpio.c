/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Digital pins on nrf54l: GPIO ports P0, P1 and P2, port index 0-2 in the
 * board's table (boards/nrf54l15-dk/board.c), bit n pin n of that port.
 *
 * A mode is one PIN_CNF write: output, input, or input with the pull-up the
 * DK's buttons need. The input buffer stays connected in every mode, so a
 * read of an output returns the level on the pin. A coordinate this SoC has
 * no pin at is TFW_ERR_BAD_PIN (soc_tables.c says which exist): the board
 * table should never produce one, and a wrong table must fail loudly, not
 * drive the wrong pin.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_tables.h"

static const uint32_t port_base[3] = {
    TFW_NRF_P0_BASE, TFW_NRF_P1_BASE, TFW_NRF_P2_BASE,
};

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *pin, tfw_pin_mode_t mode)
{
    uint32_t cnf;

    if (!tfw_nrf54l_pin_exists(pin->port, pin->bit)) {
        return TFW_ERR_BAD_PIN;
    }
    switch (mode) {
    case TFW_PIN_OUTPUT:       cnf = TFW_NRF_PIN_CNF_OUTPUT; break;
    case TFW_PIN_INPUT:        cnf = TFW_NRF_PIN_CNF_INPUT; break;
    case TFW_PIN_INPUT_PULLUP: cnf = TFW_NRF_PIN_CNF_INPUT | TFW_NRF_PIN_CNF_PULLUP; break;
    default:                   return TFW_ERR_UNSUPPORTED;
    }
    TFW_NRF_GPIO_PIN_CNF(port_base[pin->port], pin->bit) = cnf;
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *pin, uint8_t level)
{
    uint32_t bit;

    if (!tfw_nrf54l_pin_exists(pin->port, pin->bit)) {
        return TFW_ERR_BAD_PIN;
    }
    bit = 1uL << pin->bit;
    if (level) {
        TFW_NRF_GPIO_OUTSET(port_base[pin->port]) = bit;
    } else {
        TFW_NRF_GPIO_OUTCLR(port_base[pin->port]) = bit;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *pin, uint8_t *level)
{
    if (!tfw_nrf54l_pin_exists(pin->port, pin->bit)) {
        return TFW_ERR_BAD_PIN;
    }
    *level = (uint8_t)((TFW_NRF_GPIO_IN(port_base[pin->port]) >> pin->bit) & 1u);
    return TFW_OK;
}
