/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Tone: a 50 % square wave on any GPIO, from an LEDC timer and channel the
 * PWM table never names (the last of each), so a tone never steals a PWM
 * output. One tone at a time, as on the AVR.
 */

#include "driver/ledc.h"

#include "tfw/hal.h"

#define TONE_TIMER   ((ledc_timer_t)(LEDC_TIMER_MAX - 1))
#define TONE_CHANNEL ((ledc_channel_t)(LEDC_CHANNEL_MAX - 1))
#define TONE_MODE    LEDC_LOW_SPEED_MODE

static uint8_t running;

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    if (hz == 0u) {
        return tfw_hal_tone_stop();
    }
    {
        const ledc_timer_config_t timer = {
            .speed_mode      = TONE_MODE,
            .timer_num       = TONE_TIMER,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .freq_hz         = hz,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        const ledc_channel_config_t chan = {
            .gpio_num   = p->gpio,
            .speed_mode = TONE_MODE,
            .channel    = TONE_CHANNEL,
            .timer_sel  = TONE_TIMER,
            .duty       = 512u,
            .hpoint     = 0,
        };

        if (ledc_timer_config(&timer) != ESP_OK) {
            return TFW_ERR_RANGE;       /* a frequency the timer cannot divide to */
        }
        if (ledc_channel_config(&chan) != ESP_OK) {
            return TFW_ERR_INTERNAL;
        }
    }
    running = 1u;
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void)
{
    if (running) {
        (void)ledc_stop(TONE_MODE, TONE_CHANNEL, 0u);
        running = 0u;
    }
    return TFW_OK;
}
