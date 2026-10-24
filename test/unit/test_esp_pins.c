/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 pin HALs on the host (#163): src/hal/esp32/hal_gpio.c,
 * hal_pulse.c, hal_pwm.c, hal_tone.c and hal_adc.c against the gpio, LEDC and
 * oneshot ADC drivers and esp_timer clock in test/mock/esp_pins_mock.c. Each test pins one branch: the driver arguments
 * a call makes, a refusal that makes none, or the status a driver error
 * becomes. The clock is the mock's, so no test waits.
 */
#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "esp_pins_mock.h"

static const tfw_pinref_t pin = { 21u };

static uint16_t iv[4];
static uint8_t  n;

void setUp(void)
{
    (void)tfw_hal_tone_stop();      /* hal_tone.c's running flag outlives a test */
    (void)tfw_hal_adc_set_ref(TFW_ADC_REF_VCC);
    mock_pins_reset();
    for (n = 0u; n < 4u; n++) {
        iv[n] = 0xA5A5u;
    }
    n = 0xEEu;
}

void tearDown(void)
{
}

/* --- hal_gpio.c: tfw_hal_gpio_mode ------------------------------------ */

static void test_an_output_is_reset_and_set_input_output_with_no_pull(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.resets);
    TEST_ASSERT_EQUAL_INT(21, mock_pins.reset_pin);
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.directions);
    TEST_ASSERT_EQUAL_INT(21, mock_pins.direction_pin);
    TEST_ASSERT_EQUAL_INT(GPIO_MODE_INPUT_OUTPUT, mock_pins.direction);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.pulls);
}

static void test_an_input_pullup_is_an_input_with_pullup_only(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT_PULLUP));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.resets);
    TEST_ASSERT_EQUAL_INT(GPIO_MODE_INPUT, mock_pins.direction);
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.pulls);
    TEST_ASSERT_EQUAL_INT(21, mock_pins.pull_pin);
    TEST_ASSERT_EQUAL_INT(GPIO_PULLUP_ONLY, mock_pins.pull);
}

static void test_an_input_is_an_input_left_floating(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.resets);
    TEST_ASSERT_EQUAL_INT(GPIO_MODE_INPUT, mock_pins.direction);
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.pulls);
    TEST_ASSERT_EQUAL_INT(GPIO_FLOATING, mock_pins.pull);
}

static void test_the_last_valid_gpio_is_configured(void)
{
    const tfw_pinref_t last = { 54u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&last, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_INT(54, mock_pins.reset_pin);
}

static void test_a_gpio_past_the_part_is_refused_and_the_driver_is_not_called(void)
{
    const tfw_pinref_t past = { 55u };
    const tfw_pinref_t top  = { 63u };

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&past, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&top, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.resets);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.directions);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.pulls);
}

static void test_a_gpio_past_the_mask_width_is_refused_and_the_driver_is_not_called(void)
{
    const tfw_pinref_t wide = { 64u };
    const tfw_pinref_t top  = { 255u };

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&wide, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&top, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.resets);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.directions);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.pulls);
}

static void test_an_unknown_gpio_mode_is_refused_and_the_pin_is_not_reset(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      tfw_hal_gpio_mode(&pin, (tfw_pin_mode_t)3));
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.resets);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.directions);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.pulls);
}

static void test_a_reset_error_is_internal(void)
{
    mock_pins.reset_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_gpio_mode(&pin, TFW_PIN_OUTPUT));
}

static void test_a_direction_error_is_internal(void)
{
    mock_pins.direction_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
}

static void test_a_pull_error_is_internal(void)
{
    mock_pins.pull_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT_PULLUP));
}

/* --- hal_gpio.c: write and read -------------------------------------- */

static void test_a_write_of_any_nonzero_level_sets_the_pin_high(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&pin, 0x80u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.sets);
    TEST_ASSERT_EQUAL_INT(21, mock_pins.set_pin);
    TEST_ASSERT_EQUAL_UINT32(1u, mock_pins.set_level);
}

static void test_a_write_of_zero_sets_the_pin_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&pin, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, mock_pins.set_level);
}

static void test_a_set_level_error_is_a_bad_pin(void)
{
    mock_pins.set_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&pin, 1u));
}

static void test_a_read_returns_the_level_on_the_pin(void)
{
    uint8_t v = 0xEEu;

    mock_pins.level0 = 1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&pin, &v));
    TEST_ASSERT_EQUAL_UINT8(1u, v);
    TEST_ASSERT_EQUAL_INT(21, mock_pins.get_pin);

    mock_pins.level0 = 0;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&pin, &v));
    TEST_ASSERT_EQUAL_UINT8(0u, v);
}

/* --- hal_pulse.c: tfw_hal_pulse_in ------------------------------------ */

static void test_a_high_pulse_is_timed_from_rise_to_fall(void)
{
    uint32_t us = 0u;

    mock_pins_edge(100);
    mock_pins_edge(350);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_EQUAL_UINT32(250u, us);
    TEST_ASSERT_EQUAL_INT(21, mock_pins.get_pin);
}

static void test_a_low_pulse_is_timed_from_fall_to_rise(void)
{
    uint32_t us = 0u;

    mock_pins.level0 = 1;
    mock_pins_edge(100);
    mock_pins_edge(250);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 0u, 1000u, &us));
    TEST_ASSERT_EQUAL_UINT32(150u, us);
}

static void test_a_pulse_in_progress_is_not_measured_the_next_one_is(void)
{
    uint32_t us = 0u;

    mock_pins.level0 = 1;           /* high when called: that pulse ends at 50 */
    mock_pins_edge(50);
    mock_pins_edge(200);
    mock_pins_edge(260);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_EQUAL_UINT32(60u, us);
}

static void test_a_pulse_in_progress_that_never_ends_times_out(void)
{
    uint32_t us = 0xEEEEu;

    mock_pins.level0 = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_EQUAL_UINT32(0xEEEEu, us);
}

static void test_a_pulse_that_never_starts_times_out(void)
{
    uint32_t us = 0xEEEEu;

    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_EQUAL_UINT32(0xEEEEu, us);
}

static void test_a_pulse_that_never_ends_times_out(void)
{
    uint32_t us = 0xEEEEu;

    mock_pins_edge(100);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_EQUAL_UINT32(0xEEEEu, us);
}

/* --- hal_pulse.c: tfw_hal_pulse_capture ------------------------------- */

static void test_a_capture_keeps_every_interval_until_the_line_goes_quiet(void)
{
    mock_pins.level0 = 1;
    mock_pins_edge(100);
    mock_pins_edge(300);
    mock_pins_edge(600);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(3u, n);
    TEST_ASSERT_EQUAL_UINT16(90u, iv[0]);
    TEST_ASSERT_EQUAL_UINT16(200u, iv[1]);
    TEST_ASSERT_EQUAL_UINT16(300u, iv[2]);
    TEST_ASSERT_EQUAL_UINT16(0xA5A5u, iv[3]);
}

static void test_a_capture_from_a_low_start_waits_for_the_rise(void)
{
    mock_pins_edge(100);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(1u, n);
    TEST_ASSERT_EQUAL_UINT16(90u, iv[0]);
}

static void test_a_capture_stops_at_max_edges(void)
{
    mock_pins.level0 = 1;
    mock_pins_edge(100);
    mock_pins_edge(300);
    mock_pins_edge(600);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 2u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(2u, n);
    TEST_ASSERT_EQUAL_UINT16(90u, iv[0]);
    TEST_ASSERT_EQUAL_UINT16(200u, iv[1]);
    TEST_ASSERT_EQUAL_UINT16(0xA5A5u, iv[2]);
}

static void test_a_capture_interval_past_16_bits_is_clamped(void)
{
    mock_pins.step   = 1000;
    mock_pins.level0 = 1;
    mock_pins_edge(80000);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 100000u, &n));
    TEST_ASSERT_EQUAL_UINT8(1u, n);
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, iv[0]);
}

static void test_a_capture_whose_start_level_never_comes_times_out_empty(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_UINT16(0xA5A5u, iv[0]);
}

static void test_a_capture_with_no_edge_after_the_start_times_out_empty(void)
{
    mock_pins.level0 = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_UINT16(0xA5A5u, iv[0]);
}

static void test_a_capture_of_no_edges_times_out_empty(void)
{
    mock_pins.level0 = 1;
    mock_pins_edge(100);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, iv, 0u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_UINT16(0xA5A5u, iv[0]);
}

/* --- hal_pwm.c --------------------------------------------------------- */

static const tfw_pwmref_t pwm = { 2u, 17u, 1u, 0u };   /* channel, gpio, timer, mode */

static void test_a_pwm_write_configures_its_timer_at_8_bits_and_1_khz(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&pwm, 128u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.timer_configs);
    TEST_ASSERT_EQUAL_INT(LEDC_LOW_SPEED_MODE, mock_pins.timer.speed_mode);
    TEST_ASSERT_EQUAL_INT(LEDC_TIMER_1, mock_pins.timer.timer_num);
    TEST_ASSERT_EQUAL_INT(LEDC_TIMER_8_BIT, mock_pins.timer.duty_resolution);
    TEST_ASSERT_EQUAL_UINT32(1000u, mock_pins.timer.freq_hz);
    TEST_ASSERT_EQUAL_INT(LEDC_AUTO_CLK, mock_pins.timer.clk_cfg);
}

static void test_a_pwm_write_configures_its_channel_with_the_duty(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&pwm, 255u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.channel_configs);
    TEST_ASSERT_EQUAL_INT(17, mock_pins.channel.gpio_num);
    TEST_ASSERT_EQUAL_INT(LEDC_LOW_SPEED_MODE, mock_pins.channel.speed_mode);
    TEST_ASSERT_EQUAL_INT(LEDC_CHANNEL_2, mock_pins.channel.channel);
    TEST_ASSERT_EQUAL_INT(LEDC_TIMER_1, mock_pins.channel.timer_sel);
    TEST_ASSERT_EQUAL_UINT32(255u, mock_pins.channel.duty);
    TEST_ASSERT_EQUAL_INT(0, mock_pins.channel.hpoint);
}

static void test_a_pwm_timer_error_is_internal_and_the_channel_is_not_configured(void)
{
    mock_pins.timer_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_write(&pwm, 10u));
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.channel_configs);
}

static void test_a_pwm_channel_error_is_internal(void)
{
    mock_pins.channel_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_write(&pwm, 10u));
}

static void test_a_pwm_stop_stops_its_channel_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&pwm));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.stops);
    TEST_ASSERT_EQUAL_INT(LEDC_LOW_SPEED_MODE, mock_pins.stop_mode);
    TEST_ASSERT_EQUAL_INT(LEDC_CHANNEL_2, mock_pins.stop_channel);
    TEST_ASSERT_EQUAL_UINT32(0u, mock_pins.stop_idle);
}

static void test_a_pwm_stop_error_is_internal(void)
{
    mock_pins.stop_err = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pwm_stop(&pwm));
}

/* --- hal_tone.c -------------------------------------------------------- */

static void test_a_tone_takes_the_last_timer_at_10_bits_and_the_frequency(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.timer_configs);
    TEST_ASSERT_EQUAL_INT(LEDC_LOW_SPEED_MODE, mock_pins.timer.speed_mode);
    TEST_ASSERT_EQUAL_INT(LEDC_TIMER_3, mock_pins.timer.timer_num);
    TEST_ASSERT_EQUAL_INT(LEDC_TIMER_10_BIT, mock_pins.timer.duty_resolution);
    TEST_ASSERT_EQUAL_UINT32(440u, mock_pins.timer.freq_hz);
    TEST_ASSERT_EQUAL_INT(LEDC_AUTO_CLK, mock_pins.timer.clk_cfg);
}

static void test_a_tone_takes_the_last_channel_at_half_duty_on_its_pin(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.channel_configs);
    TEST_ASSERT_EQUAL_INT(21, mock_pins.channel.gpio_num);
    TEST_ASSERT_EQUAL_INT(LEDC_LOW_SPEED_MODE, mock_pins.channel.speed_mode);
    TEST_ASSERT_EQUAL_INT(LEDC_CHANNEL_7, mock_pins.channel.channel);
    TEST_ASSERT_EQUAL_INT(LEDC_TIMER_3, mock_pins.channel.timer_sel);
    TEST_ASSERT_EQUAL_UINT32(512u, mock_pins.channel.duty);
    TEST_ASSERT_EQUAL_INT(0, mock_pins.channel.hpoint);
}

static void test_a_tone_stop_after_a_start_stops_the_tone_channel_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.stops);
    TEST_ASSERT_EQUAL_INT(LEDC_LOW_SPEED_MODE, mock_pins.stop_mode);
    TEST_ASSERT_EQUAL_INT(LEDC_CHANNEL_7, mock_pins.stop_channel);
    TEST_ASSERT_EQUAL_UINT32(0u, mock_pins.stop_idle);
}

static void test_a_tone_stop_with_no_tone_running_calls_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.stops);
}

static void test_a_second_tone_stop_calls_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.stops);
}

static void test_a_tone_of_0_hz_is_a_stop_and_configures_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    mock_pins_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.stops);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.timer_configs);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.channel_configs);
}

static void test_a_tone_timer_error_is_a_frequency_out_of_range(void)
{
    mock_pins.timer_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&pin, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.channel_configs);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.stops);    /* nothing was left running */
}

static void test_a_tone_channel_error_is_internal_and_leaves_no_tone_running(void)
{
    mock_pins.channel_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_tone_start(&pin, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.stops);
}

static void test_a_tone_stop_error_is_ignored_and_the_tone_is_off(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    mock_pins.stop_err = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.stops);
}

/* --- hal_adc.c --------------------------------------------------------
 * The unit is created on the first read that passes the channel check and
 * kept for the life of the program, so these run in order: the refusal
 * before the first creation, then the creation, then the rest on that unit.
 * A unit that fails to come up is test_esp_adc.c's, in a program of its own. */

static void test_adc_init_calls_no_driver(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.new_units);
}

static void test_an_adc_channel_past_the_board_is_refused_and_no_unit_is_made(void)
{
    uint16_t raw = 0xEEEEu;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read((uint8_t)TFW_ADC_COUNT, &raw));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(0xFFu, &raw));
    TEST_ASSERT_EQUAL_UINT16(0xEEEEu, raw);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.new_units);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.adc_configs);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.adc_reads);
}

static void test_the_first_adc_read_makes_unit_1_and_reads_at_12_db(void)
{
    uint16_t raw = 0u;

    mock_pins.raw = 4095;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(3u, &raw));
    TEST_ASSERT_EQUAL_UINT16(4095u, raw);
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.new_units);
    TEST_ASSERT_EQUAL_INT(ADC_UNIT_1, mock_pins.unit_id);
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.adc_configs);
    TEST_ASSERT_EQUAL_PTR(mock_adc_unit, mock_pins.config_handle);
    TEST_ASSERT_EQUAL_INT(ADC_CHANNEL_3, mock_pins.config_channel);
    TEST_ASSERT_EQUAL_INT(ADC_ATTEN_DB_12, mock_pins.config.atten);
    TEST_ASSERT_EQUAL_INT(ADC_BITWIDTH_DEFAULT, mock_pins.config.bitwidth);
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.adc_reads);
    TEST_ASSERT_EQUAL_PTR(mock_adc_unit, mock_pins.read_handle);
    TEST_ASSERT_EQUAL_INT(ADC_CHANNEL_3, mock_pins.read_channel);
}

static void test_a_later_adc_read_keeps_the_unit_it_made(void)
{
    uint16_t raw = 0u;

    mock_pins.raw = 1234;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_UINT16(1234u, raw);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.new_units);
    TEST_ASSERT_EQUAL_PTR(mock_adc_unit, mock_pins.read_handle);
}

static void test_the_last_adc_channel_is_read(void)
{
    uint16_t raw = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read((uint8_t)(TFW_ADC_COUNT - 1u), &raw));
    TEST_ASSERT_EQUAL_INT(ADC_CHANNEL_7, mock_pins.read_channel);
}

static void test_the_internal_reference_reads_at_0_db(void)
{
    uint16_t raw = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(1u, &raw));
    TEST_ASSERT_EQUAL_INT(ADC_ATTEN_DB_0, mock_pins.config.atten);
}

static void test_an_external_reference_is_unsupported_and_keeps_the_attenuation(void)
{
    uint16_t raw = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(1u, &raw));
    TEST_ASSERT_EQUAL_INT(ADC_ATTEN_DB_0, mock_pins.config.atten);
}

static void test_an_adc_channel_config_error_is_out_of_range_and_nothing_is_read(void)
{
    uint16_t raw = 0xEEEEu;

    mock_pins.config_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(2u, &raw));
    TEST_ASSERT_EQUAL_UINT16(0xEEEEu, raw);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.adc_reads);
}

static void test_an_adc_read_error_is_a_timeout(void)
{
    uint16_t raw = 0xEEEEu;

    mock_pins.read_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(2u, &raw));
    TEST_ASSERT_EQUAL_UINT16(0xEEEEu, raw);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_output_is_reset_and_set_input_output_with_no_pull);
    RUN_TEST(test_an_input_pullup_is_an_input_with_pullup_only);
    RUN_TEST(test_an_input_is_an_input_left_floating);
    RUN_TEST(test_the_last_valid_gpio_is_configured);
    RUN_TEST(test_a_gpio_past_the_part_is_refused_and_the_driver_is_not_called);
    RUN_TEST(test_a_gpio_past_the_mask_width_is_refused_and_the_driver_is_not_called);
    RUN_TEST(test_an_unknown_gpio_mode_is_refused_and_the_pin_is_not_reset);
    RUN_TEST(test_a_reset_error_is_internal);
    RUN_TEST(test_a_direction_error_is_internal);
    RUN_TEST(test_a_pull_error_is_internal);
    RUN_TEST(test_a_write_of_any_nonzero_level_sets_the_pin_high);
    RUN_TEST(test_a_write_of_zero_sets_the_pin_low);
    RUN_TEST(test_a_set_level_error_is_a_bad_pin);
    RUN_TEST(test_a_read_returns_the_level_on_the_pin);
    RUN_TEST(test_a_high_pulse_is_timed_from_rise_to_fall);
    RUN_TEST(test_a_low_pulse_is_timed_from_fall_to_rise);
    RUN_TEST(test_a_pulse_in_progress_is_not_measured_the_next_one_is);
    RUN_TEST(test_a_pulse_in_progress_that_never_ends_times_out);
    RUN_TEST(test_a_pulse_that_never_starts_times_out);
    RUN_TEST(test_a_pulse_that_never_ends_times_out);
    RUN_TEST(test_a_capture_keeps_every_interval_until_the_line_goes_quiet);
    RUN_TEST(test_a_capture_from_a_low_start_waits_for_the_rise);
    RUN_TEST(test_a_capture_stops_at_max_edges);
    RUN_TEST(test_a_capture_interval_past_16_bits_is_clamped);
    RUN_TEST(test_a_capture_whose_start_level_never_comes_times_out_empty);
    RUN_TEST(test_a_capture_with_no_edge_after_the_start_times_out_empty);
    RUN_TEST(test_a_capture_of_no_edges_times_out_empty);
    RUN_TEST(test_a_pwm_write_configures_its_timer_at_8_bits_and_1_khz);
    RUN_TEST(test_a_pwm_write_configures_its_channel_with_the_duty);
    RUN_TEST(test_a_pwm_timer_error_is_internal_and_the_channel_is_not_configured);
    RUN_TEST(test_a_pwm_channel_error_is_internal);
    RUN_TEST(test_a_pwm_stop_stops_its_channel_low);
    RUN_TEST(test_a_pwm_stop_error_is_internal);
    RUN_TEST(test_a_tone_takes_the_last_timer_at_10_bits_and_the_frequency);
    RUN_TEST(test_a_tone_takes_the_last_channel_at_half_duty_on_its_pin);
    RUN_TEST(test_a_tone_stop_after_a_start_stops_the_tone_channel_low);
    RUN_TEST(test_a_tone_stop_with_no_tone_running_calls_nothing);
    RUN_TEST(test_a_second_tone_stop_calls_nothing);
    RUN_TEST(test_a_tone_of_0_hz_is_a_stop_and_configures_nothing);
    RUN_TEST(test_a_tone_timer_error_is_a_frequency_out_of_range);
    RUN_TEST(test_a_tone_channel_error_is_internal_and_leaves_no_tone_running);
    RUN_TEST(test_a_tone_stop_error_is_ignored_and_the_tone_is_off);
    RUN_TEST(test_adc_init_calls_no_driver);
    RUN_TEST(test_an_adc_channel_past_the_board_is_refused_and_no_unit_is_made);
    RUN_TEST(test_the_first_adc_read_makes_unit_1_and_reads_at_12_db);
    RUN_TEST(test_a_later_adc_read_keeps_the_unit_it_made);
    RUN_TEST(test_the_last_adc_channel_is_read);
    RUN_TEST(test_the_internal_reference_reads_at_0_db);
    RUN_TEST(test_an_external_reference_is_unsupported_and_keeps_the_attenuation);
    RUN_TEST(test_an_adc_channel_config_error_is_out_of_range_and_nothing_is_read);
    RUN_TEST(test_an_adc_read_error_is_a_timeout);
    return UNITY_END();
}
