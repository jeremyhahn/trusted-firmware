/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/ledc.h, for
 * test/unit/test_esp_pwm_mutate.c alone: the types, values and calls
 * src/hal/esp32/hal_pwm.c uses, with ESP-IDF's values for the original
 * ESP32 (two speed modes, four timers, eight channels). The test implements
 * the calls.
 */
#ifndef TFW_MOCK_ESP32_PWM_DRIVER_LEDC_H
#define TFW_MOCK_ESP32_PWM_DRIVER_LEDC_H

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LEDC_HIGH_SPEED_MODE = 0,
    LEDC_LOW_SPEED_MODE  = 1,
    LEDC_SPEED_MODE_MAX  = 2,
} ledc_mode_t;

typedef enum {
    LEDC_TIMER_0 = 0,
    LEDC_TIMER_1,
    LEDC_TIMER_2,
    LEDC_TIMER_3,
    LEDC_TIMER_MAX,
} ledc_timer_t;

typedef enum {
    LEDC_CHANNEL_0 = 0,
    LEDC_CHANNEL_1,
    LEDC_CHANNEL_2,
    LEDC_CHANNEL_3,
    LEDC_CHANNEL_4,
    LEDC_CHANNEL_5,
    LEDC_CHANNEL_6,
    LEDC_CHANNEL_7,
    LEDC_CHANNEL_MAX,
} ledc_channel_t;

typedef enum {
    LEDC_TIMER_1_BIT  = 1,
    LEDC_TIMER_8_BIT  = 8,
    LEDC_TIMER_10_BIT = 10,
    LEDC_TIMER_12_BIT = 12,
} ledc_timer_bit_t;

typedef enum {
    LEDC_AUTO_CLK = 0,
    LEDC_USE_APB_CLK,
    LEDC_USE_RC_FAST_CLK,
} ledc_clk_cfg_t;

typedef enum {
    LEDC_INTR_DISABLE = 0,
    LEDC_INTR_FADE_END,
} ledc_intr_type_t;

typedef struct {
    ledc_mode_t      speed_mode;
    ledc_timer_bit_t duty_resolution;
    ledc_timer_t     timer_num;
    uint32_t         freq_hz;
    ledc_clk_cfg_t   clk_cfg;
    int              deconfigure;
} ledc_timer_config_t;

typedef struct {
    int              gpio_num;
    ledc_mode_t      speed_mode;
    ledc_channel_t   channel;
    ledc_intr_type_t intr_type;
    ledc_timer_t     timer_sel;
    uint32_t         duty;
    int              hpoint;
} ledc_channel_config_t;

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf);
esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf);
esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level);

#endif
