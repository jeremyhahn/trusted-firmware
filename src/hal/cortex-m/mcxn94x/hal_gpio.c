/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Digital pins on the MCXN94x: port index n in the board's table
 * (boards/frdm-mcxn947/board.c) is RGPIO bank GPIOn and its pin controller
 * PORTn, bit b pin Pn_b. hal_soc.c has clocked and released every bank.
 *
 * A mode is two writes: the pin's PCR, which selects the GPIO function
 * (function 0), the pull and the input buffer, and the bank's direction
 * register. The input buffer is on in every mode, so a read is the pin's
 * level (PDIR) whatever the direction, and an output reads back what it
 * drives. A write is the bank's set or clear register, one store that leaves
 * the bank's other pins alone.
 *
 * A coordinate this part has no pin at is TFW_ERR_BAD_PIN: the board table
 * should never produce one, and a wrong table must fail loudly, not drive the
 * wrong pin.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"

static uint8_t is_pin(const tfw_pinref_t *pin)
{
    return (uint8_t)(pin->port < TFW_MCXN_PORT_COUNT && pin->bit < 32u);
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *pin, tfw_pin_mode_t mode)
{
    uint32_t bit;

    if (!is_pin(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    bit = 1u << pin->bit;
    switch (mode) {
    case TFW_PIN_OUTPUT:
        TFW_MCXN_PORT_PCR(pin->port, pin->bit) = TFW_MCXN_PCR_IBE;
        TFW_MCXN_GPIO_PDDR(pin->port) |= bit;
        return TFW_OK;
    case TFW_PIN_INPUT:
        TFW_MCXN_GPIO_PDDR(pin->port) &= ~bit;
        TFW_MCXN_PORT_PCR(pin->port, pin->bit) = TFW_MCXN_PCR_IBE;
        return TFW_OK;
    case TFW_PIN_INPUT_PULLUP:
        TFW_MCXN_GPIO_PDDR(pin->port) &= ~bit;
        TFW_MCXN_PORT_PCR(pin->port, pin->bit) = TFW_MCXN_PCR_IBE | TFW_MCXN_PCR_PE |
                                                 TFW_MCXN_PCR_PS;
        return TFW_OK;
    default:
        return TFW_ERR_UNSUPPORTED;
    }
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *pin, uint8_t level)
{
    uint32_t bit;

    if (!is_pin(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    bit = 1u << pin->bit;
    if (level) {
        TFW_MCXN_GPIO_PSOR(pin->port) = bit;
    } else {
        TFW_MCXN_GPIO_PCOR(pin->port) = bit;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *pin, uint8_t *level)
{
    if (!is_pin(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    *level = (uint8_t)((TFW_MCXN_GPIO_PDIR(pin->port) >> pin->bit) & 1u);
    return TFW_OK;
}
