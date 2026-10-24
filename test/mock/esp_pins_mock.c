/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The gpio, LEDC and oneshot ADC drivers and esp_timer clock behind the ESP32
 * pin HALs, on the host.
 * See esp32/esp_pins_mock.h.
 */
#include <string.h>
#include "esp_pins_mock.h"
#include "esp_timer.h"

mock_pins_t mock_pins;

void mock_pins_reset(void)
{
    memset(&mock_pins, 0, sizeof mock_pins);
    mock_pins.step = 10;
}

void mock_pins_edge(int64_t t)
{
    if (mock_pins.edges < MOCK_PINS_EDGES) {
        mock_pins.edge[mock_pins.edges++] = t;
    }
}

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    mock_pins.resets++;
    mock_pins.reset_pin = gpio_num;
    return mock_pins.reset_err;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    mock_pins.directions++;
    mock_pins.direction_pin = gpio_num;
    mock_pins.direction     = mode;
    return mock_pins.direction_err;
}

esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
    mock_pins.pulls++;
    mock_pins.pull_pin = gpio_num;
    mock_pins.pull     = pull;
    return mock_pins.pull_err;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    mock_pins.sets++;
    mock_pins.set_pin   = gpio_num;
    mock_pins.set_level = level;
    return mock_pins.set_err;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    int    level = mock_pins.level0;
    size_t i;

    mock_pins.gets++;
    mock_pins.get_pin = gpio_num;
    for (i = 0u; i < mock_pins.edges; i++) {
        if (mock_pins.edge[i] <= mock_pins.now) {
            level = !level;
        }
    }
    return level;
}

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf)
{
    mock_pins.timer_configs++;
    mock_pins.timer = *timer_conf;
    return mock_pins.timer_err;
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
    mock_pins.channel_configs++;
    mock_pins.channel = *ledc_conf;
    return mock_pins.channel_err;
}

esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level)
{
    mock_pins.stops++;
    mock_pins.stop_mode    = speed_mode;
    mock_pins.stop_channel = channel;
    mock_pins.stop_idle    = idle_level;
    return mock_pins.stop_err;
}

struct adc_oneshot_unit_ctx_t {
    uint8_t id;
};

static struct adc_oneshot_unit_ctx_t adc_unit;

adc_oneshot_unit_handle_t const mock_adc_unit = &adc_unit;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config,
                               adc_oneshot_unit_handle_t *ret_unit)
{
    mock_pins.new_units++;
    mock_pins.unit_id = init_config->unit_id;
    if (mock_pins.new_unit_err != ESP_OK) {
        return mock_pins.new_unit_err;
    }
    *ret_unit = mock_adc_unit;
    return ESP_OK;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *config)
{
    mock_pins.adc_configs++;
    mock_pins.config_handle  = handle;
    mock_pins.config_channel = channel;
    mock_pins.config         = *config;
    return mock_pins.config_err;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw)
{
    mock_pins.adc_reads++;
    mock_pins.read_handle  = handle;
    mock_pins.read_channel = chan;
    if (mock_pins.read_err != ESP_OK) {
        return mock_pins.read_err;
    }
    *out_raw = mock_pins.raw;
    return ESP_OK;
}

int64_t esp_timer_get_time(void)
{
    int64_t t = mock_pins.now;

    mock_pins.now += mock_pins.step;
    return t;
}
