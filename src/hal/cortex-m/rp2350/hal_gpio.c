/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Digital pins on the RP2350: the single-cycle I/O block (SIO), port index 0
 * in the board's table (boards/rp2350-pico2w/board.c), bit n GPIOn.
 *
 * A mode sets the pad (soc_calc.h's value for it), hands the pin to SIO
 * through IO_BANK0's function select, then sets or clears its output enable.
 * SIO has separate set and clear registers, so a write touches one pin and
 * needs no read-modify-write and no critical section.
 *
 * The RP2350A has GPIO0-29; a coordinate past them is TFW_ERR_BAD_PIN, so a
 * wrong board table fails loudly. Which of the 30 a board exposes is its
 * table's business: the Pico 2 W's lists only its header pins, not the ones
 * wired to the CYW43439 or UART0.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_calc.h"

#define PORT_GPIO  0u
#define GPIO_COUNT 30u

static uint8_t is_gpio(const tfw_pinref_t *pin)
{
    return (uint8_t)(pin->port == PORT_GPIO && pin->bit < GPIO_COUNT);
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *pin, tfw_pin_mode_t mode)
{
    uint32_t bit;

    if (!is_gpio(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    if (mode != TFW_PIN_INPUT && mode != TFW_PIN_OUTPUT &&
        mode != TFW_PIN_INPUT_PULLUP) {
        return TFW_ERR_UNSUPPORTED;
    }
    bit = 1u << pin->bit;
    TFW_CM_REG(TFW_RP_PADS_GPIO(pin->bit)) =
        tfw_rp_pad_value((uint8_t)(mode == TFW_PIN_INPUT_PULLUP));
    TFW_RP_IO_CTRL(pin->bit) = TFW_RP_FUNC_SIO;
    if (mode == TFW_PIN_OUTPUT) {
        TFW_RP_SIO_GPIO_OE_SET = bit;
    } else {
        TFW_RP_SIO_GPIO_OE_CLR = bit;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *pin, uint8_t level)
{
    uint32_t bit;

    if (!is_gpio(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    bit = 1u << pin->bit;
    if (level) {
        TFW_RP_SIO_GPIO_OUT_SET = bit;
    } else {
        TFW_RP_SIO_GPIO_OUT_CLR = bit;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *pin, uint8_t *level)
{
    if (!is_gpio(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    *level = (uint8_t)((TFW_RP_SIO_GPIO_IN >> pin->bit) & 1u);
    return TFW_OK;
}
