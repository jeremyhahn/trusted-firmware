/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * GPIO, through ESP-IDF's gpio driver. A tfw_pinref_t is a GPIO number; the
 * board table resolved it, so nothing here looks anything up.
 *
 * An output is configured INPUT_OUTPUT so that a read of it returns the level
 * on the pin, which is what a read of an AVR output returns too.
 */

#include "driver/gpio.h"

#include "tfw/hal.h"

/* GPIO_IS_VALID_GPIO shifts 1ULL by the pin number, which is undefined at 64
 * or more, so a pin past the part is refused before the mask is consulted. */
static int gpio_pin_valid(gpio_num_t n)
{
    return n < GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(n);
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *p, tfw_pin_mode_t mode)
{
    gpio_num_t n = (gpio_num_t)p->gpio;
    esp_err_t  err;

    if (!gpio_pin_valid(n)) {
        return TFW_ERR_BAD_PIN;
    }
    /* A refusal touches nothing: the mode is checked before the reset. */
    switch (mode) {
    case TFW_PIN_OUTPUT:
    case TFW_PIN_INPUT_PULLUP:
    case TFW_PIN_INPUT:
        break;
    default:
        return TFW_ERR_RANGE;
    }
    err = gpio_reset_pin(n);
    if (mode == TFW_PIN_OUTPUT) {
        err |= gpio_set_direction(n, GPIO_MODE_INPUT_OUTPUT);
    } else {
        err |= gpio_set_direction(n, GPIO_MODE_INPUT);
        err |= gpio_set_pull_mode(n, mode == TFW_PIN_INPUT_PULLUP
                                     ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
    }
    return err == ESP_OK ? TFW_OK : TFW_ERR_INTERNAL;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *p, uint8_t level)
{
    return gpio_set_level((gpio_num_t)p->gpio, level ? 1u : 0u) == ESP_OK
           ? TFW_OK : TFW_ERR_BAD_PIN;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *p, uint8_t *out)
{
    *out = (uint8_t)(gpio_get_level((gpio_num_t)p->gpio) ? 1u : 0u);
    return TFW_OK;
}
