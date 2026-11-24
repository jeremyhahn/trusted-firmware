/* SPDX-License-Identifier: Apache-2.0 */
/*
 * #561: TONE, then MODE on the same pin, then PULSE_CAPTURE. MODE's
 * gpio_reset_pin takes the pin off the LEDC channel, so the tone hal_tone.c
 * tracked has ended and the capture must not loop back a pin that is now an
 * input. hal_tone.c, hal_gpio.c and hal_pulse_rmt.c linked together over
 * stand-in gpio and LEDC drivers and the mock RMT driver.
 */
#include "unity.h"
#include "tfw/hal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_rmt_mock.h"

#include "hal_esp32.h"

#define SYM(l0, d0, l1, d1) \
    { { .duration0 = (d0), .level0 = (l0), .duration1 = (d1), .level1 = (l1) } }

static const tfw_pinref_t pin   = { 21u };
static const tfw_pinref_t other = { 22u };
static uint16_t iv[4];
static uint8_t  n;
static unsigned stops;

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    (void)gpio_num;
    return ESP_OK;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    (void)gpio_num;
    (void)mode;
    return ESP_OK;
}

esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
    (void)gpio_num;
    (void)pull;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_num;
    (void)level;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    (void)gpio_num;
    return 0;
}

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf)
{
    (void)timer_conf;
    return ESP_OK;
}

/* The fake LEDC: a configured channel toggles its pin until a stop that
 * works. A stop can fail (stop_err), or report success and keep toggling
 * (stop_sticks): the two ways NOTONE can leave the output running. */
static uint8_t   toggling;
static esp_err_t stop_err;
static uint8_t   stop_sticks;

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
    (void)ledc_conf;
    toggling = 1u;
    return ESP_OK;
}

esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level)
{
    (void)speed_mode;
    (void)channel;
    (void)idle_level;
    stops++;
    if (stop_err == ESP_OK && !stop_sticks) {
        toggling = 0u;
    }
    return stop_err;
}

void setUp(void)
{
    stop_err    = ESP_OK;
    stop_sticks = 0u;
    tfw_esp_tone_release(tfw_esp_tone_gpio());
    (void)tfw_hal_tone_stop();
    mock_rmt_reset();
    stops    = 0u;
    toggling = 0u;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, mock_rmt.misuse);
    TEST_ASSERT_EQUAL_UINT(mock_rmt.created, mock_rmt.deleted);
}

static void capture_loop_back(unsigned want)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 80, 0, 0) };

    mock_rmt_reset();
    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT(want, mock_rmt.cfg.flags.io_loop_back);
}

/* The Codex P1 on #561: MODE input after TONE leaves no tone behind. */
static void test_mode_input_after_tone_ends_the_loop_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    capture_loop_back(1u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    capture_loop_back(0u);
}

/* MODE on another pin leaves the tone, and its loop back, as they were. */
static void test_mode_on_another_pin_keeps_the_tone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&other, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(0u, stops);
    capture_loop_back(1u);
}

/* A refused MODE touches nothing, the tone included. */
static void test_a_refused_mode_keeps_the_tone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_gpio_mode(&pin, (tfw_pin_mode_t)0x7F));
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(0u, stops);
}

/* #561: a stopped tone still owns its pin; MODE takes it back with no second stop. */
static void test_mode_input_after_notone_takes_the_pin_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    capture_loop_back(0u);
}

/* #561: MODE on another pin after NOTONE leaves the stopped tone's pin owned. */
static void test_mode_on_another_pin_after_notone_keeps_the_pin(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&other, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    capture_loop_back(1u);
}

/* #48: MODE's tone release past the plain running-tone case. The release
 * after NOTONE is test_mode_input_after_notone_takes_the_pin_back (it kills
 * `if (running && owned_gpio == gpio)` and an inner `if (1)`). The two below:
 * - a tone moved to another pin is released only on the pin it now owns:
 *   kills a release that drops the `owned_gpio == gpio` compare (MODE on the
 *   old pin would stop the tone) or keys it on the first pin the tone took;
 * - MODE over a stop the driver refuses still takes the pin back, and the
 *   channel stays running so NOTONE retries the stop: kills a release that
 *   keeps owned_gpio on a failed stop (the loop back on an input pin, #561)
 *   and a NOTONE that marks the channel stopped when ledc_stop failed. */
static void test_a_tone_moved_to_another_pin_is_released_only_there(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&other, 1000u));
    TEST_ASSERT_EQUAL_INT(22, tfw_esp_tone_gpio());
    capture_loop_back(0u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(22, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(0u, stops);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&other, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    TEST_ASSERT_EQUAL_UINT(1u, stops);
}

static void test_mode_over_a_refused_stop_releases_the_pin_and_a_retry_stops_the_tone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    stop_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_UINT(1u, stops);
    TEST_ASSERT_EQUAL_INT(-1, tfw_esp_tone_gpio());
    capture_loop_back(0u);

    stop_err = ESP_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(2u, stops);
    TEST_ASSERT_EQUAL_UINT(0u, toggling);
}

/* The edges a capture after NOTONE reads on the pin: the LEDC waveform when
 * the channel still toggles and the capture kept the pad's output
 * (io_loop_back); none otherwise, since an input-only RMT pad disables the
 * output -- the measurement would silence the pin itself (Codex P1, #561). */
static uint8_t edges_after_notone(void)
{
    static const rmt_symbol_word_t wave[] = {
        SYM(1, 500, 0, 500), SYM(1, 500, 0, 0),
    };

    mock_rmt_reset();
    mock_rmt_chunk(wave, 2u, 1u);
    n = 0u;
    (void)tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n);
    TEST_ASSERT_EQUAL_UINT(1u, mock_rmt.cfg.flags.io_loop_back);
    return (toggling && mock_rmt.cfg.flags.io_loop_back) ? n : 0u;
}

/* Fault injection: a stop that fails is reported, and the capture after it
 * still sees the edges, so TestFirmwareTone's silence check fails. */
static void test_a_failed_stop_is_reported_and_its_edges_seen(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    stop_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_INT(21, tfw_esp_tone_gpio());
    TEST_ASSERT_TRUE(edges_after_notone() > 0u);
}

/* Fault injection: a stop the driver accepts but the output ignores. */
static void test_an_ineffective_stop_shows_its_edges(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    stop_sticks = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_TRUE(edges_after_notone() > 0u);
}

/* A stop that works: the same capture, loop back kept, reads silence. */
static void test_a_working_stop_reads_silence(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(0u, edges_after_notone());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_stop_is_reported_and_its_edges_seen);
    RUN_TEST(test_an_ineffective_stop_shows_its_edges);
    RUN_TEST(test_a_working_stop_reads_silence);
    RUN_TEST(test_mode_input_after_tone_ends_the_loop_back);
    RUN_TEST(test_mode_on_another_pin_keeps_the_tone);
    RUN_TEST(test_a_refused_mode_keeps_the_tone);
    RUN_TEST(test_mode_input_after_notone_takes_the_pin_back);
    RUN_TEST(test_mode_on_another_pin_after_notone_keeps_the_pin);
    RUN_TEST(test_a_tone_moved_to_another_pin_is_released_only_there);
    RUN_TEST(test_mode_over_a_refused_stop_releases_the_pin_and_a_retry_stops_the_tone);
    return UNITY_END();
}
