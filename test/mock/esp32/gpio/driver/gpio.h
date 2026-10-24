/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/gpio.h, for
 * test/unit/test_esp_gpio_mutate.c alone: the modes, pulls and calls
 * src/hal/esp32/hal_gpio.c uses, with ESP-IDF's values. GPIO_IS_VALID_GPIO
 * reads a mask the test sets, where ESP-IDF reads SOC_GPIO_VALID_GPIO_MASK.
 * The test implements the calls.
 */
#ifndef TFW_MOCK_ESP32_GPIO_DRIVER_GPIO_H
#define TFW_MOCK_ESP32_GPIO_DRIVER_GPIO_H

#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;

extern uint64_t mock_gpio_valid_mask;

#define SOC_GPIO_PIN_COUNT 55
/* The last member of ESP-IDF's gpio_num_t: one past the part's last pin. */
#define GPIO_NUM_MAX       SOC_GPIO_PIN_COUNT

#define GPIO_IS_VALID_GPIO(gpio_num)                  \
    ((gpio_num) >= 0 && (gpio_num) < 64 &&            \
     ((1ULL << (gpio_num)) & mock_gpio_valid_mask) != 0)

typedef enum {
    GPIO_MODE_DISABLE         = 0,
    GPIO_MODE_INPUT           = 1,
    GPIO_MODE_OUTPUT          = 2,
    GPIO_MODE_INPUT_OUTPUT    = 3,
    GPIO_MODE_OUTPUT_OD       = 6,
    GPIO_MODE_INPUT_OUTPUT_OD = 7,
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_ONLY,
    GPIO_PULLDOWN_ONLY,
    GPIO_PULLUP_PULLDOWN,
    GPIO_FLOATING,
} gpio_pull_mode_t;

esp_err_t gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int       gpio_get_level(gpio_num_t gpio_num);

#endif
