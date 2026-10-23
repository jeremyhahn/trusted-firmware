/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/gpio.h, for
 * test/unit/test_esp_board_mutate.c and test_esp_board_nocoproc_mutate.c
 * alone: the config struct, values and calls src/hal/esp32/hal_board.c uses,
 * with ESP-IDF's values. The tests implement the calls.
 */
#ifndef TFW_MOCK_ESP32_BOARD_DRIVER_GPIO_H
#define TFW_MOCK_ESP32_BOARD_DRIVER_GPIO_H

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
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLUP_ENABLE  = 1,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_PULLDOWN_ENABLE  = 1,
} gpio_pulldown_t;

typedef enum {
    GPIO_INTR_DISABLE    = 0,
    GPIO_INTR_POSEDGE    = 1,
    GPIO_INTR_NEGEDGE    = 2,
    GPIO_INTR_ANYEDGE    = 3,
    GPIO_INTR_LOW_LEVEL  = 4,
    GPIO_INTR_HIGH_LEVEL = 5,
} gpio_int_type_t;

typedef struct {
    uint64_t        pin_bit_mask;
    gpio_mode_t     mode;
    gpio_pullup_t   pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *pGPIOConfig);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
esp_err_t gpio_hold_en(gpio_num_t gpio_num);

#endif
