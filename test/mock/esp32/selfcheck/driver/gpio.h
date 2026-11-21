/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/gpio.h, the calls
 * src/hal/esp32/hal_selfcheck.c makes on the co-processor enable, over one
 * modelled pad (test/unit/test_esp_selfcheck.c keeps it). As on silicon,
 * gpio_get_level() reads 0 while the pad's input buffer is off, and a held
 * pad ignores a direction change.
 */
#ifndef TFW_MOCK_SELFCHECK_GPIO_H
#define TFW_MOCK_SELFCHECK_GPIO_H

#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT = 1,
    GPIO_MODE_OUTPUT = 2,
    GPIO_MODE_INPUT_OUTPUT = 3,
} gpio_mode_t;

esp_err_t gpio_hold_en(gpio_num_t gpio_num);
esp_err_t gpio_hold_dis(gpio_num_t gpio_num);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
int gpio_get_level(gpio_num_t gpio_num);

#endif
