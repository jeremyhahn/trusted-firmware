/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/caps/analog.c, pulse.c, pwm.c and tone.c under mutation (#163). Each
 * test names the mutants it kills: a bound moved by one, a check dropped, a
 * call skipped, or a status not passed back. test_analog, test_pulse and
 * test_pwm cover the behaviour; these pin the edges those leave open.
 */

#include <string.h>
#include "unity.h"
#include "analog.h"
#include "pulse.h"
#include "pwm.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

/* The most intervals one reply carries: a count byte, then u16s. pulse.c
 * keeps its own copy private; docs/device-capabilities/api.md states it. */
enum { CAPTURE_MAX = (TFW_MAX_PAYLOAD - 1u) / 2u };

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_mock_reset(); }
void tearDown(void) {}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    memset(outbuf, 0xEE, sizeof outbuf);
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t pulse_in(uint8_t pin, uint8_t level, uint32_t timeout)
{
    uint8_t p[6] = { pin, level, (uint8_t)timeout, (uint8_t)(timeout >> 8),
                     (uint8_t)(timeout >> 16), (uint8_t)(timeout >> 24) };
    return run(TFW_CMD_PULSE_IN, p, 6u);
}

static tfw_status_t capture(uint8_t pin, uint8_t start, uint8_t max, uint32_t timeout)
{
    uint8_t p[7] = { pin, start, max, (uint8_t)timeout, (uint8_t)(timeout >> 8),
                     (uint8_t)(timeout >> 16), (uint8_t)(timeout >> 24) };
    return run(TFW_CMD_PULSE_CAPTURE, p, 7u);
}

static tfw_status_t tone(uint8_t pin, uint16_t hz)
{
    uint8_t p[3] = { pin, (uint8_t)hz, (uint8_t)(hz >> 8) };
    return run(TFW_CMD_TONE, p, 3u);
}

/* ---- analog.c ---------------------------------------------------------- */

/* Kills: a channel taken from the pin number rather than the board map, a
 * fixed channel, a skipped write, and a byte order swapped. */
static void test_analog_read_returns_the_mapped_channel_little_endian(void)
{
    tfw_mock_set_adc(0u, 0x0011u);
    tfw_mock_set_adc(5u, 0x03A5u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 19u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(2u, w.pos);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, outbuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, outbuf[1]);
}

/* Kills: a dropped check on the read's status (the reply would carry a raw
 * value the converter never produced). */
static void test_a_failed_conversion_is_passed_back_with_no_reply(void)
{
    tfw_mock_set_adc(0u, 0x0123u);
    tfw_mock_adc_read_fails(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 14u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: a dropped check on the board's channel lookup. */
static void test_a_digital_only_pin_is_refused_with_no_reply(void)
{
    TEST_ASSERT_NOT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 13u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 99u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: the bound moved down (> EXTERNAL, >= INTERNAL). */
static void test_every_reference_up_to_internal_is_accepted(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ADC_REF, (const uint8_t[]){ TFW_ADC_REF_VCC }, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ADC_REF, (const uint8_t[]){ TFW_ADC_REF_EXTERNAL }, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ADC_REF, (const uint8_t[]){ TFW_ADC_REF_INTERNAL }, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      run(TFW_CMD_ADC_REF, (const uint8_t[]){ TFW_ADC_REF_INTERNAL + 1u }, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_ADC_REF, (const uint8_t[]){ 0xFFu }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: set_ref skipped (the status is the mock's OK either way, so only the
 * reference it holds tells). VCC is 0, the reset value, so it goes last. */
static void test_each_accepted_reference_reaches_the_hal(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ADC_REF, (const uint8_t[]){ TFW_ADC_REF_INTERNAL }, 1u));
    TEST_ASSERT_EQUAL_UINT8(TFW_ADC_REF_INTERNAL, tfw_mock_adc_ref());
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ADC_REF, (const uint8_t[]){ TFW_ADC_REF_EXTERNAL }, 1u));
    TEST_ASSERT_EQUAL_UINT8(TFW_ADC_REF_EXTERNAL, tfw_mock_adc_ref());
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ADC_REF, (const uint8_t[]){ TFW_ADC_REF_VCC }, 1u));
    TEST_ASSERT_EQUAL_UINT8(TFW_ADC_REF_VCC, tfw_mock_adc_ref());
    TEST_ASSERT_EQUAL_UINT(3u, tfw_mock_adc_ref_calls());
}

/* Kills: the bound moved up (> INTERNAL + 1) or dropped. The mock's set_ref
 * refuses past INTERNAL too, so the status alone cannot tell; the handler
 * must refuse before the HAL is called at all. */
static void test_a_reference_past_internal_never_reaches_the_hal(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      run(TFW_CMD_ADC_REF, (const uint8_t[]){ TFW_ADC_REF_INTERNAL + 1u }, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_ADC_REF, (const uint8_t[]){ 0xFFu }, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_adc_ref_calls());
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* ---- pwm.c ------------------------------------------------------------- */

/* Kills: a dropped IN_USE term (pin 1 has no compare unit, so the board would
 * answer UNSUPPORTED instead) and the UART's pin made an output. */
static void test_pwm_on_the_uart_pin_is_refused_as_reserved(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 1u, 128u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 1u));
}

/* Kills: a dropped check on the board's compare lookup, and the pin made an
 * output before it: pin 5 is the tick timer's, and must be left alone. */
static void test_pwm_on_the_tick_pin_is_unsupported_and_leaves_the_pin(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 5u, 128u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 5u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 13u, 128u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PINB, 5u));
}

/* Kills: duty 1 taken as a stop (== 0 moved to <= 1), and duty 0 written as a
 * compare value rather than disconnecting the unit. */
static void test_duty_one_drives_and_duty_zero_disconnects(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 1u }, 2u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pwm_duty(TFW_ADDR_OCR1AL));
    TEST_ASSERT_TRUE(tfw_mock_pwm_connected(TFW_ADDR_TCCR1A, COM1A1_BIT));
    TEST_ASSERT_TRUE(tfw_mock_pin_is_output(TFW_ADDR_PINB, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 0u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pwm_connected(TFW_ADDR_TCCR1A, COM1A1_BIT));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pwm_duty(TFW_ADDR_OCR1AL));
}

/* Kills: gpio_mode's status ignored (the unit would be driven on a pin that
 * never became an output, and the host told OK). */
static void test_a_pin_that_cannot_be_made_an_output_is_not_driven(void)
{
    tfw_mock_gpio_mode_fails_once(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 77u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PINB, 1u));
    TEST_ASSERT_FALSE(tfw_mock_pwm_connected(TFW_ADDR_TCCR1A, COM1A1_BIT));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pwm_duty(TFW_ADDR_OCR1AL));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 77u }, 2u));
    TEST_ASSERT_EQUAL_UINT8(77u, tfw_mock_pwm_duty(TFW_ADDR_OCR1AL));
}

/* ---- tone.c ------------------------------------------------------------ */

/* Kills: the pin check dropped or inverted. */
static void test_tone_only_on_its_one_pin(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tone(TFW_TONE_PIN - 1u, 1000u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tone(TFW_TONE_PIN + 1u, 1000u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
    TEST_ASSERT_EQUAL(TFW_OK, tone(TFW_TONE_PIN, 1000u));
    TEST_ASSERT_EQUAL_UINT16(1000u, tfw_mock_tone_hz());
}

/* Kills: the frequency's byte order swapped and the HAL's refusal not passed
 * back. */
static void test_tone_frequency_is_little_endian_and_refusal_passes_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tone(TFW_TONE_PIN, 0x1234u));
    TEST_ASSERT_EQUAL_UINT16(0x1234u, tfw_mock_tone_hz());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tone(TFW_TONE_PIN, 489u));
}

/* Kills: a stop that does not reach the HAL. */
static void test_no_tone_silences(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tone(TFW_TONE_PIN, 2000u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_NO_TONE, 0, 0u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* ---- pulse.c ----------------------------------------------------------- */

/* Kills: level bound moved (> 0, > 2) and a dropped level check. */
static void test_pulse_level_is_zero_or_one(void)
{
    tfw_mock_set_pulse_in(100u);
    TEST_ASSERT_EQUAL(TFW_OK, pulse_in(2u, 0u, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, pulse_in(2u, 1u, 1000u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, pulse_in(2u, 2u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: the timeout bound moved either way, the zero check dropped, and the
 * reply's width written short or in the wrong byte order. */
static void test_pulse_timeout_bounds_and_reply(void)
{
    tfw_mock_set_pulse_in(TFW_PULSE_MAX_US);
    TEST_ASSERT_EQUAL(TFW_OK, pulse_in(2u, 1u, TFW_PULSE_MAX_US));
    TEST_ASSERT_EQUAL_UINT8(4u, w.pos);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)TFW_PULSE_MAX_US, outbuf[0]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(TFW_PULSE_MAX_US >> 8), outbuf[1]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(TFW_PULSE_MAX_US >> 16), outbuf[2]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(TFW_PULSE_MAX_US >> 24), outbuf[3]);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, pulse_in(2u, 1u, TFW_PULSE_MAX_US + 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, pulse_in(2u, 1u, 0u));
    tfw_mock_set_pulse_in(1u);
    TEST_ASSERT_EQUAL(TFW_OK, pulse_in(2u, 1u, 1u));
}

/* Kills: a dropped pin check, and a HAL timeout written as a width. */
static void test_pulse_bad_pin_and_timeout_leave_no_reply(void)
{
    tfw_mock_set_pulse_in(100u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, pulse_in(99u, 1u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    tfw_mock_set_pulse_in(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, pulse_in(2u, 1u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: the start bound moved or dropped, and a dropped pin check. */
static void test_capture_start_is_zero_or_one(void)
{
    static const uint16_t us[2] = { 26u, 70u };

    tfw_mock_set_capture(us, 2u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, capture(99u, 1u, 2u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_EQUAL(TFW_OK, capture(2u, 0u, 2u, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, capture(2u, 1u, 2u, 1000u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, capture(2u, 2u, 2u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: the edge bound moved either way, the zero check dropped, and the
 * reply's count or intervals written short. */
static void test_capture_edge_bounds_and_reply(void)
{
    uint16_t us[32];
    uint8_t  i;
    uint8_t  n = CAPTURE_MAX < 32u ? (uint8_t)CAPTURE_MAX : 32u;

    for (i = 0u; i < 32u; i++) { us[i] = (uint16_t)(0x0100u + i); }
    tfw_mock_set_capture(us, n);
    TEST_ASSERT_EQUAL(TFW_OK, capture(2u, 1u, (uint8_t)CAPTURE_MAX, TFW_PULSE_MAX_US));
    TEST_ASSERT_EQUAL_UINT8(1u + 2u * n, w.pos);
    TEST_ASSERT_EQUAL_UINT8(n, outbuf[0]);
    for (i = 0u; i < n; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)us[i], outbuf[1u + 2u * i]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(us[i] >> 8), outbuf[2u + 2u * i]);
    }
    TEST_ASSERT_EQUAL(TFW_OK, capture(2u, 1u, 1u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(3u, w.pos);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, capture(2u, 1u, (uint8_t)(CAPTURE_MAX + 1u), 1000u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, capture(2u, 1u, 0u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: the capture timeout bound moved either way or the zero check
 * dropped, and a HAL timeout written as a reply. */
static void test_capture_timeout_bounds(void)
{
    static const uint16_t us[1] = { 26u };

    tfw_mock_set_capture(us, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, capture(2u, 1u, 1u, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, capture(2u, 1u, 1u, TFW_PULSE_MAX_US + 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, capture(2u, 1u, 1u, 0u));
    tfw_mock_set_capture(us, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, capture(2u, 1u, 1u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_analog_read_returns_the_mapped_channel_little_endian);
    RUN_TEST(test_a_failed_conversion_is_passed_back_with_no_reply);
    RUN_TEST(test_a_digital_only_pin_is_refused_with_no_reply);
    RUN_TEST(test_every_reference_up_to_internal_is_accepted);
    RUN_TEST(test_each_accepted_reference_reaches_the_hal);
    RUN_TEST(test_a_reference_past_internal_never_reaches_the_hal);
    RUN_TEST(test_pwm_on_the_uart_pin_is_refused_as_reserved);
    RUN_TEST(test_pwm_on_the_tick_pin_is_unsupported_and_leaves_the_pin);
    RUN_TEST(test_duty_one_drives_and_duty_zero_disconnects);
    RUN_TEST(test_a_pin_that_cannot_be_made_an_output_is_not_driven);
    RUN_TEST(test_tone_only_on_its_one_pin);
    RUN_TEST(test_tone_frequency_is_little_endian_and_refusal_passes_back);
    RUN_TEST(test_no_tone_silences);
    RUN_TEST(test_pulse_level_is_zero_or_one);
    RUN_TEST(test_pulse_timeout_bounds_and_reply);
    RUN_TEST(test_pulse_bad_pin_and_timeout_leave_no_reply);
    RUN_TEST(test_capture_start_is_zero_or_one);
    RUN_TEST(test_capture_edge_bounds_and_reply);
    RUN_TEST(test_capture_timeout_bounds);
    return UNITY_END();
}
