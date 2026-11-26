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
 *   - duties 0 to 254 passed whole, and duty 255 as the full 8-bit period
 *     (256), so the pin is high all cycle as on the AVR;
 *   - a timer that will not configure failing the write with no channel
 *     configured, a channel that will not configure failing it too;
 *   - a (speed mode, timer) configured once, on its first good write, and
 *     later writes on it, from any channel, configuring only the channel;
 *     a refused timer tried again on the next write. The record persists
 *     across the tests, so each test uses its own (mode, timer) pair;
 *   - a stop as the row's channel stopped low, a failed stop internal.
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "driver/ledc.h"
#include "tfw/hal.h"
#include "hal_esp32.h"

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
    tfw_esp_pwm_release(2);
    tfw_esp_pwm_release(3);
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
        TEST_ASSERT_EQUAL_UINT32(duty[i] == 255u ? 256u : duty[i], chan_seen.duty);
        TEST_ASSERT_EQUAL_INT(0, chan_seen.hpoint);
        TEST_ASSERT_EQUAL(LEDC_INTR_DISABLE, chan_seen.intr_type);
        TEST_ASSERT_EQUAL_UINT(0u, n_stop);
    }
}

static void test_the_duty_is_passed_whole(void)
{
    static const uint8_t duty[] = { 0u, 1u, 127u, 254u };
    const tfw_pwmref_t   p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };
    unsigned i;

    for (i = 0u; i < sizeof duty; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, duty[i]));
        TEST_ASSERT_EQUAL_UINT32(duty[i], chan_seen.duty);
    }
}

static void test_duty_255_is_the_full_period(void)
{
    const tfw_pwmref_t p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, 255u));
    TEST_ASSERT_EQUAL_UINT32(1u << 8, chan_seen.duty);
}

static void test_a_timer_it_cannot_configure_fails_with_no_channel(void)
{
    static const esp_err_t errs[] = { ESP_ERR_INVALID_ARG, ESP_FAIL };
    const tfw_pwmref_t     p = { 1u, 2u, 1u, (uint8_t)LEDC_HIGH_SPEED_MODE };
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
    const tfw_pwmref_t     p = { 1u, 2u, 1u, (uint8_t)LEDC_LOW_SPEED_MODE };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        n_timer = n_chan = 0u;
        chan_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_write(&p, 9u));
        TEST_ASSERT_EQUAL_UINT(i == 0u ? 1u : 0u, n_timer);
        TEST_ASSERT_EQUAL_UINT(1u, n_chan);
    }
}

static void test_a_second_channel_leaves_the_shared_timer_alone(void)
{
    const tfw_pwmref_t a = { 0u, 2u, 0u, (uint8_t)LEDC_HIGH_SPEED_MODE };
    const tfw_pwmref_t b = { 1u, 4u, 0u, (uint8_t)LEDC_HIGH_SPEED_MODE };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&a, 10u));
    TEST_ASSERT_EQUAL_UINT(1u, n_timer);
    TEST_ASSERT_EQUAL_UINT(1u, n_chan);

    n_timer = n_chan = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&b, 20u));
    TEST_ASSERT_EQUAL_UINT(0u, n_timer);
    TEST_ASSERT_EQUAL_UINT(1u, n_chan);
    TEST_ASSERT_EQUAL(LEDC_CHANNEL_1, chan_seen.channel);
    TEST_ASSERT_EQUAL(LEDC_TIMER_0, chan_seen.timer_sel);
    TEST_ASSERT_EQUAL_UINT32(20u, chan_seen.duty);

    n_timer = n_chan = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&a, 30u));
    TEST_ASSERT_EQUAL_UINT(0u, n_timer);
    TEST_ASSERT_EQUAL_UINT(1u, n_chan);
}

static void test_a_refused_timer_is_configured_on_the_next_write(void)
{
    const tfw_pwmref_t p = { 2u, 5u, 2u, (uint8_t)LEDC_HIGH_SPEED_MODE };

    timer_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL_UINT(1u, n_timer);
    TEST_ASSERT_EQUAL_UINT(0u, n_chan);

    timer_err = ESP_OK;
    n_timer = n_chan = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL_UINT(1u, n_timer);
    TEST_ASSERT_EQUAL_UINT(1u, n_chan);

    n_timer = n_chan = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL_UINT(0u, n_timer);
    TEST_ASSERT_EQUAL_UINT(1u, n_chan);
}

static void test_each_mode_keeps_its_own_timers(void)
{
    const tfw_pwmref_t p = { 4u, 2u, 3u, (uint8_t)LEDC_LOW_SPEED_MODE };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL_UINT(1u, n_timer);
    TEST_ASSERT_EQUAL(LEDC_LOW_SPEED_MODE, timer_seen.speed_mode);
    TEST_ASSERT_EQUAL(LEDC_TIMER_3, timer_seen.timer_num);

    n_timer = n_chan = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL_UINT(0u, n_timer);
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

/* #48: a write that LEDC accepted marks its pin driven, so the RMT capture
 * loops it back; another pin stays undriven. */
static void test_a_write_marks_its_pin_driven(void)
{
    const tfw_pwmref_t p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };

    TEST_ASSERT_EQUAL_INT(0, tfw_esp_pwm_drives(2));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL_INT(1, tfw_esp_pwm_drives(2));
    TEST_ASSERT_EQUAL_INT(0, tfw_esp_pwm_drives(3));
}

/* ledc_stop leaves the pad routed to LEDC at idle: the pin stays driven. */
static void test_a_stop_keeps_the_pin_driven(void)
{
    const tfw_pwmref_t p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&p));
    TEST_ASSERT_EQUAL_INT(1, tfw_esp_pwm_drives(2));
}

/* A refused timer or channel routes nothing: the pin is not marked. The row
 * is the high-speed mode's timer 1, the one timer no earlier test configures
 * (test_a_timer_it_cannot_configure_fails_with_no_channel refuses it), so the
 * write reaches ledc_timer_config rather than the timer already up. */
static void test_a_failed_write_marks_nothing(void)
{
    const tfw_pwmref_t p = { 1u, 2u, 1u, (uint8_t)LEDC_HIGH_SPEED_MODE };

    timer_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL_INT(0, tfw_esp_pwm_drives(2));

    timer_err = ESP_OK;
    chan_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL_INT(0, tfw_esp_pwm_drives(2));
}

/* hal_gpio.c's MODE releases the pin; pins outside the bitmap are ignored. */
static void test_release_clears_the_pin(void)
{
    const tfw_pwmref_t p = { 1u, 2u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };
    const tfw_pwmref_t q = { 2u, 3u, 0u, (uint8_t)LEDC_LOW_SPEED_MODE };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&p, 9u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&q, 9u));
    tfw_esp_pwm_release(2);
    TEST_ASSERT_EQUAL_INT(0, tfw_esp_pwm_drives(2));
    TEST_ASSERT_EQUAL_INT(1, tfw_esp_pwm_drives(3));

    tfw_esp_pwm_release(-1);
    tfw_esp_pwm_release(64);
    TEST_ASSERT_EQUAL_INT(1, tfw_esp_pwm_drives(3));
    TEST_ASSERT_EQUAL_INT(0, tfw_esp_pwm_drives(-1));
    TEST_ASSERT_EQUAL_INT(0, tfw_esp_pwm_drives(64));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_write_is_the_timer_then_the_channel);
    RUN_TEST(test_the_duty_is_passed_whole);
    RUN_TEST(test_duty_255_is_the_full_period);
    RUN_TEST(test_a_timer_it_cannot_configure_fails_with_no_channel);
    RUN_TEST(test_a_channel_it_cannot_configure_fails);
    RUN_TEST(test_a_second_channel_leaves_the_shared_timer_alone);
    RUN_TEST(test_a_refused_timer_is_configured_on_the_next_write);
    RUN_TEST(test_each_mode_keeps_its_own_timers);
    RUN_TEST(test_a_stop_is_the_channel_stopped_low);
    RUN_TEST(test_a_failed_stop_is_internal);
    RUN_TEST(test_a_write_marks_its_pin_driven);
    RUN_TEST(test_a_stop_keeps_the_pin_driven);
    RUN_TEST(test_a_failed_write_marks_nothing);
    RUN_TEST(test_release_clears_the_pin);
    return UNITY_END();
}
