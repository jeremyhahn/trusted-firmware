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

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *p, tfw_pin_mode_t mode)
{
    gpio_num_t n = (gpio_num_t)p->gpio;
    esp_err_t  err;

    if (!GPIO_IS_VALID_GPIO(n)) {
        return TFW_ERR_BAD_PIN;
    }
    err = gpio_reset_pin(n);
    switch (mode) {
    case TFW_PIN_OUTPUT:
        err |= gpio_set_direction(n, GPIO_MODE_INPUT_OUTPUT);
        break;
    case TFW_PIN_INPUT_PULLUP:
        err |= gpio_set_direction(n, GPIO_MODE_INPUT);
        err |= gpio_set_pull_mode(n, GPIO_PULLUP_ONLY);
        break;
    case TFW_PIN_INPUT:
        err |= gpio_set_direction(n, GPIO_MODE_INPUT);
        err |= gpio_set_pull_mode(n, GPIO_FLOATING);
        break;
    default:
        return TFW_ERR_RANGE;
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
