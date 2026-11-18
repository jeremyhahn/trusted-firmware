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

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
    (void)ledc_conf;
    return ESP_OK;
}

esp_err_t ledc_stop(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t idle_level)
{
    (void)speed_mode;
    (void)channel;
    (void)idle_level;
    stops++;
    return ESP_OK;
}

void setUp(void)
{
    (void)tfw_hal_tone_stop();
    mock_rmt_reset();
    stops = 0u;
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mode_input_after_tone_ends_the_loop_back);
    RUN_TEST(test_mode_on_another_pin_keeps_the_tone);
    RUN_TEST(test_a_refused_mode_keeps_the_tone);
    return UNITY_END();
}
