/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/ledc.h and the hal/ledc_types.h
 * it includes: the three calls src/hal/esp32/hal_pwm.c and hal_tone.c make,
 * and the fields of the two configs they fill. test/mock/esp_pins_mock.c
 * defines the calls.
 *
 * The enums carry the esp32p4's values (no high-speed mode, four timers,
 * eight channels), the part whose board these HALs serve.
 */
#ifndef TFW_MOCK_DRIVER_LEDC_H
#define TFW_MOCK_DRIVER_LEDC_H

#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LEDC_LOW_SPEED_MODE = 0,
    LEDC_SPEED_MODE_MAX,
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
    LEDC_TIMER_8_BIT  = 8,
    LEDC_TIMER_10_BIT = 10,
} ledc_timer_bit_t;

typedef enum {
    LEDC_AUTO_CLK = 0,
} ledc_clk_cfg_t;

typedef struct {
    ledc_mode_t      speed_mode;
    ledc_timer_bit_t duty_resolution;
    ledc_timer_t     timer_num;
    uint32_t         freq_hz;
    ledc_clk_cfg_t   clk_cfg;
} ledc_timer_config_t;

typedef struct {
    int            gpio_num;
    ledc_mode_t    speed_mode;
    ledc_channel_t channel;
    ledc_timer_t   timer_sel;
    uint32_t       duty;
    int            hpoint;
} ledc_channel_config_t;

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf);
esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf);
esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level);

#endif
