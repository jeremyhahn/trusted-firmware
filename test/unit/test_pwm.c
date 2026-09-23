/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "unity.h"
#include "pwm.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "ag/board.h"
#include "ag/protocol.h"
#include "ag/config.h"
#include "board_profile.h"

static uint8_t     inbuf[AG_MAX_PAYLOAD];
static uint8_t     outbuf[AG_MAX_PAYLOAD];
static ag_reader_t r;
static ag_writer_t w;

void setUp(void)    { ag_mock_reset(); }
void tearDown(void) {}

static ag_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    ag_reader_init(&r, inbuf, n);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_dispatch(cmd, &r, &w);
}

/* Pin 9 is OC1A. Asserting on the compare register the mock keeps means the
 * board's PWM table is exercised, not just the capability module. */
static void test_writing_a_duty_reaches_the_right_compare_register(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 128u }, 2u));
    TEST_ASSERT_EQUAL_UINT8(128u, ag_mock_pwm_duty(AG_ADDR_OCR1AL));
    TEST_ASSERT_TRUE(ag_mock_pwm_connected(AG_ADDR_TCCR1A, COM1A1_BIT));
}

static void test_each_pwm_pin_drives_its_own_unit(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 3u,  10u }, 2u));
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 10u, 20u }, 2u));
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 11u, 30u }, 2u));
    TEST_ASSERT_EQUAL_UINT8(10u, ag_mock_pwm_duty(AG_ADDR_OCR2B));
    TEST_ASSERT_EQUAL_UINT8(20u, ag_mock_pwm_duty(AG_ADDR_OCR1BL));
    TEST_ASSERT_EQUAL_UINT8(30u, ag_mock_pwm_duty(AG_ADDR_OCR2A));
}

/* The pin is made an output automatically. Requiring a separate pin_mode first
 * would leave a PWM that silently does nothing when someone forgets. */
static void test_pwm_makes_the_pin_an_output(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 64u }, 2u));
    TEST_ASSERT_TRUE(ag_mock_pin_is_output(AG_ADDR_PINB, 1u));   /* pin 9 = PB1 */
}

/* A duty of zero DISCONNECTS the pin rather than setting a zero duty. Holding
 * it low through the timer would quietly prevent digital_write from working on
 * that pin afterwards. */
static void test_a_duty_of_zero_disconnects_rather_than_holding_low(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 200u }, 2u));
    TEST_ASSERT_TRUE(ag_mock_pwm_connected(AG_ADDR_TCCR1A, COM1A1_BIT));

    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 0u }, 2u));
    TEST_ASSERT_FALSE(ag_mock_pwm_connected(AG_ADDR_TCCR1A, COM1A1_BIT));
}

/* Pins 5 and 6 HAVE compare units, but Timer0 is the millisecond tick. The
 * refusal is what stops a host writing a duty into a timer counting something
 * else and wondering why nothing moves. */
static void test_the_tick_timers_pins_refuse_pwm(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 5u, 100u }, 2u));
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 6u, 100u }, 2u));
}

static void test_a_pin_with_no_compare_unit_refuses_pwm(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 13u, 100u }, 2u));
}

static void test_a_reserved_pin_refuses_pwm(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RESERVED_PIN, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 1u, 100u }, 2u));
}

static void test_a_pin_beyond_the_board_refuses_pwm(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_BAD_PIN, run(AG_CMD_PWM_WRITE, (const uint8_t[]){ 99u, 100u }, 2u));
}

static void test_tone_starts_and_stops(void)
{
    const uint8_t on[3] = { 3u, 0x10u, 0x27u };    /* 10000 Hz, little-endian */
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_TONE, on, 3u));
    TEST_ASSERT_EQUAL_UINT16(10000u, ag_mock_tone_hz());

    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_NO_TONE, (const uint8_t[]){ 3u }, 1u));
    TEST_ASSERT_EQUAL_UINT16(0u, ag_mock_tone_hz());
}

/* A frequency below what the prescaler can express is refused rather than
 * producing a tone at some other pitch entirely. */
static void test_a_frequency_below_the_reachable_range_is_refused(void)
{
    const uint8_t low[3] = { 3u, 100u, 0u };       /* 100 Hz */
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, run(AG_CMD_TONE, low, 3u));
}

static void test_tone_on_a_reserved_pin_is_refused(void)
{
    const uint8_t on[3] = { 1u, 0x10u, 0x27u };
    TEST_ASSERT_EQUAL(AG_ERR_RESERVED_PIN, run(AG_CMD_TONE, on, 3u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_writing_a_duty_reaches_the_right_compare_register);
    RUN_TEST(test_each_pwm_pin_drives_its_own_unit);
    RUN_TEST(test_pwm_makes_the_pin_an_output);
    RUN_TEST(test_a_duty_of_zero_disconnects_rather_than_holding_low);
    RUN_TEST(test_the_tick_timers_pins_refuse_pwm);
    RUN_TEST(test_a_pin_with_no_compare_unit_refuses_pwm);
    RUN_TEST(test_a_reserved_pin_refuses_pwm);
    RUN_TEST(test_a_pin_beyond_the_board_refuses_pwm);
    RUN_TEST(test_tone_starts_and_stops);
    RUN_TEST(test_a_frequency_below_the_reachable_range_is_refused);
    RUN_TEST(test_tone_on_a_reserved_pin_is_refused);
    return UNITY_END();
}
