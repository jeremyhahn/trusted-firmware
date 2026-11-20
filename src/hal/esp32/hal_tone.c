/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Tone: a 50 % square wave on any GPIO, from an LEDC timer and channel the
 * PWM table never names (the last of each), so a tone never steals a PWM
 * output. One tone at a time, as on the AVR.
 */

#include "driver/ledc.h"

#include "tfw/hal.h"

#include "hal_esp32.h"

#define TONE_TIMER   ((ledc_timer_t)(LEDC_TIMER_MAX - 1))
#define TONE_CHANNEL ((ledc_channel_t)(LEDC_CHANNEL_MAX - 1))
#define TONE_MODE    LEDC_LOW_SPEED_MODE

static uint8_t running;
static int     tone_gpio = -1;    /* the pin the LEDC channel drives */
/* The pin whose pad output the LEDC channel holds through the GPIO matrix.
 * A stop leaves it there, driving the idle level, so it outlives `running`:
 * only MODE's gpio_reset_pin takes the pin back (#561, Codex P1). */
static int     owned_gpio = -1;

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
    running    = 1u;
    tone_gpio  = p->gpio;
    owned_gpio = p->gpio;
    return TFW_OK;
}

/* A driver stop that fails is reported, and the tone stays running so a
 * retry stops it: NOTONE does not claim a silence it did not make. */
tfw_status_t tfw_hal_tone_stop(void)
{
    if (running) {
        if (ledc_stop(TONE_MODE, TONE_CHANNEL, 0u) != ESP_OK) {
            return TFW_ERR_INTERNAL;
        }
        running = 0u;
    }
    tone_gpio = -1;
    return TFW_OK;
}

/* The pin the RMT capture loops back: the one LEDC owns, running or stopped,
 * so a capture after NOTONE keeps the pad's output path and reads what the
 * channel really drives there. */
int tfw_esp_tone_gpio(void)
{
    return owned_gpio;
}

/* hal_gpio.c's MODE, before it resets the pin: gpio_reset_pin takes the pin
 * off the LEDC channel, so a tone on that pin ends here too, and the RMT
 * capture no longer loops back a pin that is now an input (#561). */
void tfw_esp_tone_release(int gpio)
{
    if (owned_gpio == gpio) {
        if (running && tone_gpio == gpio) {
            (void)tfw_hal_tone_stop();
        }
        owned_gpio = -1;
    }
}
