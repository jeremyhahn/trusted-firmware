/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * GPIO, through ESP-IDF's gpio driver. A tfw_pinref_t is a GPIO number; the
 * board table resolved it, so nothing here looks anything up.
 *
 * An output is configured INPUT_OUTPUT so that a read of it returns the level
 * on the pin, which is what a read of an AVR output returns too.
 *
 * MODE OUTPUT records the pin, so the RMT capture loops it back rather than
 * silencing its output (#48).
 */

#include "driver/gpio.h"

#include "tfw/config.h"
#include "tfw/hal.h"

#include "hal_esp32.h"

/* GPIO_IS_VALID_GPIO shifts 1ULL by the pin number, which is undefined at 64
 * or more, so a pin past the part is refused before the mask is consulted;
 * mode, write and read all check the pin here before the driver sees it. */
static int gpio_pin_valid(gpio_num_t n)
{
    return n < GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(n);
}

/* The pins MODE made an output, bit n for GPIO n (tfw_esp_gpio_drives). */
static uint64_t gpio_output;

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
#if TFW_FEATURE_TONE
    tfw_esp_tone_release((int)n);   /* the reset below takes the pin off LEDC */
#endif
#if TFW_FEATURE_PWM
    tfw_esp_pwm_release((int)n);    /* and off a LEDC PWM channel too (#48) */
#endif
    err = gpio_reset_pin(n);
    if (mode == TFW_PIN_OUTPUT) {
        err |= gpio_set_direction(n, GPIO_MODE_INPUT_OUTPUT);
    } else {
        err |= gpio_set_direction(n, GPIO_MODE_INPUT);
        err |= gpio_set_pull_mode(n, mode == TFW_PIN_INPUT_PULLUP
                                     ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
    }
    /* gpio_pin_valid keeps n below 64; an input or a failed call clears. */
    if (mode == TFW_PIN_OUTPUT && err == ESP_OK) {
        gpio_output |= 1ULL << (unsigned)n;
    } else {
        gpio_output &= ~(1ULL << (unsigned)n);
    }
    return err == ESP_OK ? TFW_OK : TFW_ERR_INTERNAL;
}

int tfw_esp_gpio_drives(int gpio)
{
    if (gpio < 0 || gpio > 63) {
        return 0;
    }
    return (gpio_output >> (unsigned)gpio) & 1ULL ? 1 : 0;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *p, uint8_t level)
{
    if (!gpio_pin_valid((gpio_num_t)p->gpio)) {
        return TFW_ERR_BAD_PIN;
    }
    return gpio_set_level((gpio_num_t)p->gpio, level ? 1u : 0u) == ESP_OK
           ? TFW_OK : TFW_ERR_BAD_PIN;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *p, uint8_t *out)
{
    if (!gpio_pin_valid((gpio_num_t)p->gpio)) {
        return TFW_ERR_BAD_PIN;
    }
    *out = (uint8_t)(gpio_get_level((gpio_num_t)p->gpio) ? 1u : 0u);
    return TFW_OK;
}
