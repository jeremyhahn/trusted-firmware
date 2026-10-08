/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "unity.h"
#include "gpio.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_mock_reset(); }
void tearDown(void) {}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

/* Pin 13 is PORTB bit 5. Asserting on the pseudo-register the mock keeps means
 * this exercises the board descriptor's arithmetic too: a table with a wrong
 * port address fails here, not only on hardware. */
static void test_writing_a_pin_sets_the_right_bit_of_the_right_port(void)
{
    const uint8_t out[2] = { 13u, 1u };
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 13u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_WRITE, out, 2u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(TFW_ADDR_PINB, 5u));
    /* and nothing on a neighbouring port moved */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(TFW_ADDR_PIND, 5u));
}

static void test_pin_mode_sets_the_direction_register(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 2u, 1u }, 2u));
    TEST_ASSERT_TRUE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 2u));

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 2u, 0u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 2u));
}

static void test_input_pullup_enables_the_pullup_without_becoming_an_output(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 2u, 2u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 2u));
    TEST_ASSERT_TRUE(tfw_mock_pin_pullup(TFW_ADDR_PIND, 2u));
}

/* Switching from input-pullup to plain input must also clear the pull-up.
 * Leaving it set produces an input that reads high with nothing connected --
 * a "working" sensor that always reports the same value. */
static void test_leaving_pullup_mode_clears_the_pullup(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 2u, 2u }, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 2u, 0u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_pullup(TFW_ADDR_PIND, 2u));
}

static void test_reading_returns_the_level_on_the_line(void)
{
    tfw_mock_set_input_level(TFW_ADDR_PIND, 2u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ 2u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(1u, outbuf[0]);

    tfw_mock_set_input_level(TFW_ADDR_PIND, 2u, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ 2u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, outbuf[0]);
}

/* Read-after-write on an OUTPUT pin must return what was written. PINx is the
 * pin synchroniser on real silicon, not an input-only latch, so it reflects the
 * driven level. Pinned here because the mock got this wrong once and sent a
 * search for a firmware bug that did not exist. */
static void test_reading_an_output_pin_returns_what_was_written(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE,      (const uint8_t[]){ 2u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 2u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_READ,  (const uint8_t[]){ 2u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(1u, outbuf[0]);

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 2u, 0u }, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_READ,  (const uint8_t[]){ 2u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, outbuf[0]);
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 99u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, run(TFW_CMD_DIGITAL_READ,  (const uint8_t[]){ 99u }, 1u));
}

/* A6 and A7 are ADC-only. Writing to one must fail rather than resolve to port
 * index zero and toggle something on PORTB instead. */
static void test_an_adc_only_pin_cannot_be_driven(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 20u, 1u }, 2u));
}

/* The UART pins are the line the command arrived on. Driving pin 1 would cut
 * it, and the host would never learn why the device stopped answering. */
static void test_the_uart_pins_cannot_be_driven(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 1u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_PIN_MODE,      (const uint8_t[]){ 0u, 1u }, 2u));
}

/* Reading them IS allowed: observing the line is harmless, and refusing it
 * would make diagnosing a wiring problem harder than it needs to be. */
static void test_a_reserved_pin_may_still_be_read(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ 1u }, 1u));
}

static void test_an_out_of_range_level_or_mode_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 2u, 7u }, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_PIN_MODE,      (const uint8_t[]){ 2u, 9u }, 2u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_writing_a_pin_sets_the_right_bit_of_the_right_port);
    RUN_TEST(test_pin_mode_sets_the_direction_register);
    RUN_TEST(test_input_pullup_enables_the_pullup_without_becoming_an_output);
    RUN_TEST(test_leaving_pullup_mode_clears_the_pullup);
    RUN_TEST(test_reading_returns_the_level_on_the_line);
    RUN_TEST(test_reading_an_output_pin_returns_what_was_written);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_an_adc_only_pin_cannot_be_driven);
    RUN_TEST(test_the_uart_pins_cannot_be_driven);
    RUN_TEST(test_a_reserved_pin_may_still_be_read);
    RUN_TEST(test_an_out_of_range_level_or_mode_is_refused);
    return UNITY_END();
}
