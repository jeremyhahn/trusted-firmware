/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 tone, src/hal/esp32/hal_tone.c.
 *
 * The unit had no host build, and no test_tone_mutate exists. This builds it
 * over a stand-in for ESP-IDF's LEDC driver (test/mock/esp32/tone/), each
 * call logged here and able to fail, and holds:
 *
 *   - a tone as the last LEDC timer at 10 bits on the low-speed mode, the
 *     frequency whole on the automatic clock, then the last channel on that
 *     timer and the pin at half duty, the phase at 0;
 *   - a timer that will not divide to the frequency out of range with no
 *     channel configured, a channel that will not configure internal, and
 *     neither leaving a tone to stop;
 *   - a refused start claims no pin and leaves a running tone, and its pin,
 *     as they were;
 *   - a stop as the last channel stopped low, once, only while a tone runs,
 *     and a failed driver stop reported as internal with the tone left
 *     running, so a retry stops it;
 *   - a tone of 0 Hz a stop.
 *
 * It also holds that a tone started on another pin, running or stopped
 * before, resets the pin the channel drove, once, so one tone sounds on one
 * pin, and that a retune on the same pin resets nothing (#48).
 *
 * The unit's running flag is static, so the tests run in the order main()
 * lists and each leaves no tone running.
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "driver/ledc.h"
#include "tfw/hal.h"
#include "hal_esp32.h"
#include "driver/gpio.h"

static unsigned              n_timer, n_chan, n_stop;
static unsigned              n_reset;
static int                   reset_seen;
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

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    n_reset++;
    reset_seen = gpio_num;
    return ESP_OK;
}

void setUp(void)
{
    stop_err = ESP_OK;
    tfw_esp_tone_release(tfw_esp_tone_gpio()); /* MODE takes the last test's pin back */
    n_timer = n_chan = n_stop = 0u;
    n_reset = 0u;
    reset_seen = -1;
    memset(&timer_seen, 0x5A, sizeof timer_seen);
    memset(&chan_seen, 0x5A, sizeof chan_seen);
    stop_mode = (ledc_mode_t)0x5A;
    stop_chan = (ledc_channel_t)0x5A;
    stop_idle = 0x5Au;
    timer_err = chan_err = stop_err = ESP_OK;
}

void tearDown(void) {}

static void assert_stopped_once(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, n_stop);
    TEST_ASSERT_EQUAL(LEDC_LOW_SPEED_MODE, stop_mode);
    TEST_ASSERT_EQUAL(LEDC_CHANNEL_7, stop_chan);
    TEST_ASSERT_EQUAL_UINT32(0u, stop_idle);
}

static void test_a_stop_with_no_tone_does_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(0u, n_timer + n_chan + n_stop);
}

static void test_0_hz_with_no_tone_does_nothing(void)
{
    const tfw_pinref_t p = { 4u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, n_timer + n_chan + n_stop);
}

static void test_a_frequency_the_timer_refuses_is_out_of_range(void)
{
    static const esp_err_t errs[] = { ESP_ERR_INVALID_ARG, ESP_FAIL };
    const tfw_pinref_t     p = { 4u };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        setUp();
        timer_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&p, 440u));
        TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());   /* a refused start claims no pin */
        TEST_ASSERT_EQUAL_UINT(1u, n_timer);
        TEST_ASSERT_EQUAL_UINT(0u, n_chan);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
        TEST_ASSERT_EQUAL_UINT(0u, n_stop);
    }
}

static void test_a_channel_it_cannot_configure_is_internal(void)
{
    static const esp_err_t errs[] = { ESP_ERR_INVALID_ARG, ESP_FAIL };
    const tfw_pinref_t     p = { 4u };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        setUp();
        chan_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_tone_start(&p, 440u));
        TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());   /* a refused start claims no pin */
        TEST_ASSERT_EQUAL_UINT(1u, n_timer);
        TEST_ASSERT_EQUAL_UINT(1u, n_chan);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
        TEST_ASSERT_EQUAL_UINT(0u, n_stop);
    }
}

static void test_a_refused_start_keeps_the_running_tone_and_its_pin(void)
{
    const tfw_pinref_t p7 = { 7u };
    const tfw_pinref_t p4 = { 4u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p7, 440u));
    timer_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&p4, 880u));
    TEST_ASSERT_EQUAL_INT(7, tfw_esp_tone_gpio());      /* the loopback stays on the tone */
    TEST_ASSERT_EQUAL_UINT(1u, n_chan);
    timer_err = ESP_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());     /* the old tone still ran */
    assert_stopped_once();
}

static void test_a_refused_channel_keeps_the_running_tones_pitch(void)
{
    const tfw_pinref_t p4 = { 4u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p4, 440u));
    chan_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_tone_start(&p4, 880u));
    TEST_ASSERT_EQUAL_UINT32(440u, timer_seen.freq_hz); /* the shared timer is put back */
    TEST_ASSERT_EQUAL_UINT(3u, n_timer);
    TEST_ASSERT_EQUAL_INT(4, tfw_esp_tone_gpio());
    chan_err = ESP_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    assert_stopped_once();
}

static void test_a_refused_channel_on_another_pin_keeps_the_running_tone(void)
{
    const tfw_pinref_t p7 = { 7u };
    const tfw_pinref_t p4 = { 4u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p7, 440u));
    chan_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_tone_start(&p4, 880u));
    TEST_ASSERT_EQUAL_UINT32(440u, timer_seen.freq_hz);
    TEST_ASSERT_EQUAL_INT(7, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(0u, n_reset);                /* the running pin is not taken back */
    chan_err = ESP_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    assert_stopped_once();
}

static void test_a_tone_is_the_last_timer_and_channel_at_half_duty(void)
{
    const tfw_pinref_t p = { 38u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p, 2093u));
    TEST_ASSERT_EQUAL_UINT(1u, n_timer);
    TEST_ASSERT_EQUAL(LEDC_LOW_SPEED_MODE, timer_seen.speed_mode);
    TEST_ASSERT_EQUAL(LEDC_TIMER_3, timer_seen.timer_num);
    TEST_ASSERT_EQUAL(LEDC_TIMER_10_BIT, timer_seen.duty_resolution);
    TEST_ASSERT_EQUAL_UINT32(2093u, timer_seen.freq_hz);
    TEST_ASSERT_EQUAL(LEDC_AUTO_CLK, timer_seen.clk_cfg);
    TEST_ASSERT_EQUAL_INT(0, timer_seen.deconfigure);

    TEST_ASSERT_EQUAL_UINT(1u, n_chan);
    TEST_ASSERT_EQUAL_INT(38, chan_seen.gpio_num);
    TEST_ASSERT_EQUAL(LEDC_LOW_SPEED_MODE, chan_seen.speed_mode);
    TEST_ASSERT_EQUAL(LEDC_CHANNEL_7, chan_seen.channel);
    TEST_ASSERT_EQUAL(LEDC_TIMER_3, chan_seen.timer_sel);
    TEST_ASSERT_EQUAL_UINT32(512u, chan_seen.duty);
    TEST_ASSERT_EQUAL_INT(0, chan_seen.hpoint);
    TEST_ASSERT_EQUAL(LEDC_INTR_DISABLE, chan_seen.intr_type);
    TEST_ASSERT_EQUAL_UINT(0u, n_stop);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    assert_stopped_once();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(1u, n_stop);
    TEST_ASSERT_EQUAL_UINT(1u, n_timer);
    TEST_ASSERT_EQUAL_UINT(1u, n_chan);
}

static void test_the_frequency_is_passed_whole(void)
{
    static const uint16_t hz[] = { 1u, 2u, 31u, 255u, 256u, 40000u, 65535u };
    const tfw_pinref_t    p = { 1u };
    unsigned i;

    for (i = 0u; i < sizeof hz / sizeof hz[0]; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p, hz[i]));
        TEST_ASSERT_EQUAL_UINT32(hz[i], timer_seen.freq_hz);
    }
    TEST_ASSERT_EQUAL_UINT(0u, n_stop);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    assert_stopped_once();
}

static void test_0_hz_stops_a_running_tone(void)
{
    const tfw_pinref_t p = { 7u };

    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p, 440u));
    TEST_ASSERT_EQUAL_INT(7, tfw_esp_tone_gpio());      /* #573: the RMT loopback pin */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p, 0u));
    /* #561: stopped, the pad is still LEDC's and still loops back. */
    TEST_ASSERT_EQUAL_INT(7, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, n_timer);
    TEST_ASSERT_EQUAL_UINT(1u, n_chan);
    assert_stopped_once();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, n_stop);
}

static void test_a_failed_driver_stop_is_reported(void)
{
    const tfw_pinref_t p = { 7u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&p, 440u));
    stop_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_tone_stop());
    assert_stopped_once();
    stop_err = ESP_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());     /* the retry stops it */
    TEST_ASSERT_EQUAL_UINT(2u, n_stop);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(2u, n_stop);
}

/* #48: ledc_channel_config routes the channel to the new pin and leaves the
 * old one routed too, so the start must take the old pin back itself. */
static void test_a_tone_on_another_pin_takes_the_old_pin_off_the_channel(void)
{
    const tfw_pinref_t a = { 7u };
    const tfw_pinref_t b = { 4u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&a, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&b, 880u));
    TEST_ASSERT_EQUAL_UINT(1u, n_reset);
    TEST_ASSERT_EQUAL_INT(7, reset_seen);
    TEST_ASSERT_EQUAL_INT(4, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    assert_stopped_once();
}

static void test_a_tone_after_notone_on_another_pin_takes_the_stopped_pin_back(void)
{
    const tfw_pinref_t a = { 7u };
    const tfw_pinref_t b = { 4u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&a, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&b, 880u));
    TEST_ASSERT_EQUAL_UINT(1u, n_reset);
    TEST_ASSERT_EQUAL_INT(7, reset_seen);
    TEST_ASSERT_EQUAL_INT(4, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
}

static void test_a_retune_on_the_same_pin_resets_nothing(void)
{
    const tfw_pinref_t a = { 7u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&a, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&a, 880u));
    TEST_ASSERT_EQUAL_UINT(0u, n_reset);
    TEST_ASSERT_EQUAL_INT(7, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    assert_stopped_once();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_stop_with_no_tone_does_nothing);
    RUN_TEST(test_0_hz_with_no_tone_does_nothing);
    RUN_TEST(test_a_frequency_the_timer_refuses_is_out_of_range);
    RUN_TEST(test_a_channel_it_cannot_configure_is_internal);
    RUN_TEST(test_a_refused_start_keeps_the_running_tone_and_its_pin);
    RUN_TEST(test_a_refused_channel_keeps_the_running_tones_pitch);
    RUN_TEST(test_a_refused_channel_on_another_pin_keeps_the_running_tone);
    RUN_TEST(test_a_tone_is_the_last_timer_and_channel_at_half_duty);
    RUN_TEST(test_the_frequency_is_passed_whole);
    RUN_TEST(test_0_hz_stops_a_running_tone);
    RUN_TEST(test_a_failed_driver_stop_is_reported);
    RUN_TEST(test_a_tone_on_another_pin_takes_the_old_pin_off_the_channel);
    RUN_TEST(test_a_tone_after_notone_on_another_pin_takes_the_stopped_pin_back);
    RUN_TEST(test_a_retune_on_the_same_pin_resets_nothing);
    return UNITY_END();
}
