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
    GPIO_MODE_INPUT_OUTPUT = 3,     /* GPIO_MODE_DEF_INPUT | GPIO_MODE_DEF_OUTPUT */
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

/*
 * What src/hal/esp32/hal_gpio.c and hal_pulse.c add; test/mock/esp_pins_mock.c
 * defines these. The valid-GPIO mask is the esp32p4's, fifty-five GPIOs; in
 * ESP-IDF it lives in soc/soc_caps.h, kept here so that header stays the RMT's.
 */
typedef enum {
    GPIO_PULLUP_ONLY     = 0,
    GPIO_PULLDOWN_ONLY   = 1,
    GPIO_PULLUP_PULLDOWN = 2,
    GPIO_FLOATING        = 3,
} gpio_pull_mode_t;

#define SOC_GPIO_PIN_COUNT       55
#define SOC_GPIO_VALID_GPIO_MASK ((1ULL << SOC_GPIO_PIN_COUNT) - 1)
/* The last member of ESP-IDF's gpio_num_t: one past the part's last pin. */
#define GPIO_NUM_MAX             SOC_GPIO_PIN_COUNT
#define GPIO_IS_VALID_GPIO(gpio_num) \
    ((gpio_num >= 0) && (((1ULL << (gpio_num)) & SOC_GPIO_VALID_GPIO_MASK) != 0))

esp_err_t gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull);
int       gpio_get_level(gpio_num_t gpio_num);

#endif
