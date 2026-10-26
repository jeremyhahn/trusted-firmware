/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Digital pins on the STM32H5: a bank index from the board's port table
 * (boards/<board>/board.c), 0 for GPIOA to 8 for GPIOI, and a pin 0-15.
 *
 * A mode switches the bank's clock on before it writes the bank: every bank
 * leaves reset unclocked, with every pin analog. A write is the bank's BSRR,
 * one store and no read-modify-write, so it cannot race the UART's pins in
 * the same bank. A read is the input register, which follows the pin in
 * output mode too.
 *
 * The level is the pin's. The board's LEDs are wired to light at a low pin
 * (docs/boards/stm32h573i-dk.md); the HAL does not invert it, as no HAL here
 * inverts a pin for what is wired to it.
 *
 * A coordinate past the part's banks or pins is TFW_ERR_BAD_PIN: the board
 * table should never produce one, and a wrong table must fail loudly.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "h5_calc.h"

static uint8_t valid(const tfw_pinref_t *pin)
{
    return (uint8_t)(pin->port < TFW_H5_GPIO_BANKS && pin->bit < TFW_H5_GPIO_PINS);
}

static uint32_t bank_of(const tfw_pinref_t *pin)
{
    return tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, pin->port);
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *pin, tfw_pin_mode_t mode)
{
    uint32_t bank;
    uint32_t moder;
    uint32_t pull;

    if (!valid(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    switch (mode) {
    case TFW_PIN_INPUT:        moder = TFW_H5_MODE_INPUT;  pull = TFW_H5_PULL_NONE; break;
    case TFW_PIN_INPUT_PULLUP: moder = TFW_H5_MODE_INPUT;  pull = TFW_H5_PULL_UP;   break;
    case TFW_PIN_OUTPUT:       moder = TFW_H5_MODE_OUTPUT; pull = TFW_H5_PULL_NONE; break;
    default:                   return TFW_ERR_UNSUPPORTED;
    }
    bank = bank_of(pin);
    TFW_H5_RCC_AHB2ENR |= 1u << pin->port;
    (void)TFW_H5_RCC_AHB2ENR;                   /* the enable lands before the bank is written */
    TFW_H5_GPIO_PUPDR(bank) = tfw_h5_field2(TFW_H5_GPIO_PUPDR(bank), pin->bit, pull);
    TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(TFW_H5_GPIO_MODER(bank), pin->bit, moder);
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *pin, uint8_t level)
{
    if (!valid(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    TFW_H5_GPIO_BSRR(bank_of(pin)) = level ? (1u << pin->bit) : (1u << (pin->bit + 16u));
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *pin, uint8_t *level)
{
    if (!valid(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    *level = (uint8_t)((TFW_H5_GPIO_IDR(bank_of(pin)) >> pin->bit) & 1u);
    return TFW_OK;
}
