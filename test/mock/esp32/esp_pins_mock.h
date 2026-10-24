/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The controls of test/mock/esp_pins_mock.c: the gpio driver calls
 * src/hal/esp32/hal_gpio.c and hal_pulse.c make, the esp_timer clock, and the
 * LEDC calls of hal_pwm.c and hal_tone.c and the oneshot ADC calls of hal_adc.c.
 *
 * Every call is counted and its arguments kept (the last call's). A non-zero
 * *_err is what the next and every later call of that function returns,
 * until reset. The pin's level is scripted against the clock: it reads
 * `level0` until the first time in `edge`, and toggles at each one. The clock
 * advances `step` microseconds per esp_timer_get_time read, and only then.
 */
#ifndef TFW_MOCK_ESP_PINS_MOCK_H
#define TFW_MOCK_ESP_PINS_MOCK_H

#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

#define MOCK_PINS_EDGES 8u

typedef struct {
    /* gpio_reset_pin */
    unsigned         resets;
    gpio_num_t       reset_pin;
    esp_err_t        reset_err;
    /* gpio_set_direction */
    unsigned         directions;
    gpio_num_t       direction_pin;
    gpio_mode_t      direction;
    esp_err_t        direction_err;
    /* gpio_set_pull_mode */
    unsigned         pulls;
    gpio_num_t       pull_pin;
    gpio_pull_mode_t pull;
    esp_err_t        pull_err;
    /* gpio_set_level */
    unsigned         sets;
    gpio_num_t       set_pin;
    uint32_t         set_level;
    esp_err_t        set_err;
    /* gpio_get_level */
    unsigned         gets;
    gpio_num_t       get_pin;
    int              level0;
    int64_t          edge[MOCK_PINS_EDGES];
    size_t           edges;
    /* ledc_timer_config, ledc_channel_config, ledc_stop */
    unsigned              timer_configs;
    ledc_timer_config_t   timer;
    esp_err_t             timer_err;
    unsigned              channel_configs;
    ledc_channel_config_t channel;
    esp_err_t             channel_err;
    unsigned              stops;
    ledc_mode_t           stop_mode;
    ledc_channel_t        stop_channel;
    uint32_t              stop_idle;
    esp_err_t             stop_err;
    /* adc_oneshot_new_unit, _config_channel, _read; the handle is mock_adc_unit */
    unsigned                  new_units;
    adc_unit_t                unit_id;
    esp_err_t                 new_unit_err;   /* *ret_unit is left alone */
    unsigned                  adc_configs;
    adc_oneshot_unit_handle_t config_handle;
    adc_channel_t             config_channel;
    adc_oneshot_chan_cfg_t    config;
    esp_err_t                 config_err;
    unsigned                  adc_reads;
    adc_oneshot_unit_handle_t read_handle;
    adc_channel_t             read_channel;
    int                       raw;
    esp_err_t                 read_err;       /* *out_raw is left alone */
    /* esp_timer_get_time */
    int64_t          now;
    int64_t          step;
} mock_pins_t;

extern mock_pins_t mock_pins;

/* The ADC unit adc_oneshot_new_unit hands out. */
extern adc_oneshot_unit_handle_t const mock_adc_unit;

/* No call, nothing injected, the pin low with no edge; the clock at 0,
 * stepping 10 us. */
void mock_pins_reset(void);

/* Appends an edge at `t` microseconds. */
void mock_pins_edge(int64_t t);

#endif
