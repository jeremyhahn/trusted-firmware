/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/gpio.h: the four calls
 * src/hal/esp32/hal_board.c makes to hold the C6's enable low. The suite
 * defines them.
 */
#ifndef TFW_MOCK_DRIVER_GPIO_H
#define TFW_MOCK_DRIVER_GPIO_H

#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT   = 1,
    GPIO_MODE_OUTPUT  = 2,
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLUP_ENABLE  = 1,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_PULLDOWN_ENABLE  = 1,
} gpio_pulldown_t;

typedef enum {
    GPIO_INTR_DISABLE = 0,
} gpio_int_type_t;

typedef struct {
    uint64_t        pin_bit_mask;
    gpio_mode_t     mode;
    gpio_pullup_t   pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
esp_err_t gpio_hold_en(gpio_num_t gpio_num);

#endif
