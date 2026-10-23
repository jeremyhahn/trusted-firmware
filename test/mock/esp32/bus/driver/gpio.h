/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/gpio.h, for
 * test/unit/test_esp_bus_mutate.c alone: the modes, pulls and calls
 * src/hal/esp32/hal_bus.c uses, with ESP-IDF's values. The test implements
 * the calls.
 */
#ifndef TFW_MOCK_ESP32_BUS_DRIVER_GPIO_H
#define TFW_MOCK_ESP32_BUS_DRIVER_GPIO_H

#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;

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

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int       gpio_get_level(gpio_num_t gpio_num);

#endif
