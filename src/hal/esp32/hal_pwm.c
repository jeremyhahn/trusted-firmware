/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * PWM through the LEDC peripheral. A tfw_pwmref_t names the channel, its
 * GPIO, its timer and the speed mode, all from the board's PWM table.
 *
 * Eight-bit duty at about 1 kHz, the resolution the wire carries and roughly
 * the frequency an Arduino's analogWrite gives, so a load moved from an AVR
 * board behaves the same. The tone generator (hal_tone.c) owns the last LEDC
 * timer; the board's PWM table never names it.
 */

#include "driver/ledc.h"

#include "tfw/hal.h"

#define PWM_HZ 1000u

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    const ledc_timer_config_t timer = {
        .speed_mode      = (ledc_mode_t)p->mode,
        .timer_num       = (ledc_timer_t)p->timer,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = PWM_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t chan = {
        .gpio_num   = p->gpio,
        .speed_mode = (ledc_mode_t)p->mode,
        .channel    = (ledc_channel_t)p->channel,
        .timer_sel  = (ledc_timer_t)p->timer,
        .duty       = duty,
        .hpoint     = 0,
    };

    if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&chan) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    return ledc_stop((ledc_mode_t)p->mode, (ledc_channel_t)p->channel, 0u) == ESP_OK
           ? TFW_OK : TFW_ERR_INTERNAL;
}
