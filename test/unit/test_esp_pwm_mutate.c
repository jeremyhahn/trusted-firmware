/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 PWM output, src/hal/esp32/hal_pwm.c.
 *
 * The unit had no host build. This builds it over a stand-in for ESP-IDF's
 * LEDC driver (test/mock/esp32/pwm/), each call logged here and able to
 * fail, and holds:
 *
 *   - a write as the row's timer at 8 bits and 1 kHz on the automatic
 *     clock, then the row's channel on that timer and pin at the duty, the
 *     phase at 0, each field from its own field of the row;
 *   - a timer that will not configure failing the write with no channel
 *     configured, a channel that will not configure failing it too;
 *   - a stop as the row's channel stopped low, a failed stop internal.
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "driver/ledc.h"
#include "tfw/hal.h"

static unsigned              n_timer, n_chan, n_stop;
static ledc_timer_config_t   timer_seen;
static ledc_channel_config_t chan_seen;
static ledc_mode_t           stop_mode;
static ledc_channel_t        stop_chan;
static uint32_t              stop_idle;
static esp_err_t             timer_err, chan_err, stop_err;

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf)
{
    n_timer++;
    timer_seen = *timer_conf;
    return timer_err;
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
    n_chan++;
    chan_seen = *ledc_conf;
    return chan_err;
}

esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level)
{
    n_stop++;
    stop_mode = speed_mode;
    stop_chan = channel;
    stop_idle = idle_level;
    return stop_err;
}

void setUp(void)
{
    n_timer = n_chan = n_stop = 0u;
    memset(&timer_seen, 0x5A, sizeof timer_seen);
    memset(&chan_seen, 0x5A, sizeof chan_seen);
    stop_mode = (ledc_mode_t)0x5A;
    stop_chan = (ledc_channel_t)0x5A;
    stop_idle = 0x5Au;
    timer_err = chan_err = stop_err = ESP_OK;
}

void tearDown(void) {}

static void test_a_write_is_the_timer_then_the_channel(void)
{
    static const tfw_pwmref_t rows[] = {
        { 5u, 18u, 2u, (uint8_t)LEDC_LOW_SPEED_MODE },
        { 0u, 33u, 3u, (uint8_t)LEDC_HIGH_SPEED_MODE },
    };
    static const uint8_t duty[] = { 128u, 255u };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        setUp();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&rows[i], duty[i]));
        TEST_ASSERT_EQUAL_UINT(1u, n_timer);
        TEST_ASSERT_EQUAL((ledc_mode_t)rows[i].mode, timer_seen.speed_mode);
        TEST_ASSERT_EQUAL((ledc_timer_t)rows[i].timer, timer_seen.timer_num);
        TEST_ASSERT_EQUAL(LEDC_TIMER_8_BIT, timer_seen.duty_resolution);
        TEST_ASSERT_EQUAL_UINT32(1000u, timer_seen.freq_hz);
        TEST_ASSERT_EQUAL(LEDC_AUTO_CLK, timer_seen.clk_cfg);
        TEST_ASSERT_EQUAL_INT(0, timer_seen.deconfigure);

        TEST_ASSERT_EQUAL_UINT(1u, n_chan);
        TEST_ASSERT_EQUAL_INT(rows[i].gpio, chan_seen.gpio_num);
        TEST_ASSERT_EQUAL((ledc_mode_t)rows[i].mode, chan_seen.speed_mode);
        TEST_ASSERT_EQUAL((ledc_channel_t)rows[i].channel, chan_seen.channel);
        TEST_ASSERT_EQUAL((ledc_timer_t)rows[i].timer, chan_seen.timer_sel);
        TEST_ASSERT_EQUAL_UINT32(duty[i], chan_seen.duty);
        TEST_ASSERT_EQUAL_INT(0, chan_seen.hpoint);
        TEST_ASSERT_EQUAL(LEDC_INTR_DISABLE, chan_seen.intr_type);
        TEST_ASSERT_EQUAL_UINT(0u, n_stop);
    }
}

static void test_the_duty_is_passed_whole(void)
{
    static const uint8_t duty[] = { 0u, 1u, 127u, 254u, 255u };
    const tfw_pwmref_t   p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };
    unsigned i;

    for (i = 0u; i < sizeof duty; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, duty[i]));
        TEST_ASSERT_EQUAL_UINT32(duty[i], chan_seen.duty);
    }
}

static void test_a_timer_it_cannot_configure_fails_with_no_channel(void)
{
    static const esp_err_t errs[] = { ESP_ERR_INVALID_ARG, ESP_FAIL };
    const tfw_pwmref_t     p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        n_timer = n_chan = 0u;
        timer_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_write(&p, 9u));
        TEST_ASSERT_EQUAL_UINT(1u, n_timer);
        TEST_ASSERT_EQUAL_UINT(0u, n_chan);
    }
}

static void test_a_channel_it_cannot_configure_fails(void)
{
    static const esp_err_t errs[] = { ESP_ERR_INVALID_ARG, ESP_FAIL };
    const tfw_pwmref_t     p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        n_timer = n_chan = 0u;
        chan_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_write(&p, 9u));
        TEST_ASSERT_EQUAL_UINT(1u, n_timer);
        TEST_ASSERT_EQUAL_UINT(1u, n_chan);
    }
}

static void test_a_stop_is_the_channel_stopped_low(void)
{
    static const tfw_pwmref_t rows[] = {
        { 6u, 18u, 2u, (uint8_t)LEDC_LOW_SPEED_MODE },
        { 3u, 21u, 1u, (uint8_t)LEDC_HIGH_SPEED_MODE },
    };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        setUp();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&rows[i]));
        TEST_ASSERT_EQUAL_UINT(1u, n_stop);
        TEST_ASSERT_EQUAL((ledc_mode_t)rows[i].mode, stop_mode);
        TEST_ASSERT_EQUAL((ledc_channel_t)rows[i].channel, stop_chan);
        TEST_ASSERT_EQUAL_UINT32(0u, stop_idle);
        TEST_ASSERT_EQUAL_UINT(0u, n_timer + n_chan);
    }
}

static void test_a_failed_stop_is_internal(void)
{
    static const esp_err_t errs[] = { ESP_ERR_INVALID_ARG, ESP_FAIL };
    const tfw_pwmref_t     p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        stop_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_stop(&p));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_write_is_the_timer_then_the_channel);
    RUN_TEST(test_the_duty_is_passed_whole);
    RUN_TEST(test_a_timer_it_cannot_configure_fails_with_no_channel);
    RUN_TEST(test_a_channel_it_cannot_configure_fails);
    RUN_TEST(test_a_stop_is_the_channel_stopped_low);
    RUN_TEST(test_a_failed_stop_is_internal);
    return UNITY_END();
}
