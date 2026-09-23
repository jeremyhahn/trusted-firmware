/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Pulse measurement and edge capture.
 *
 * The capture test decodes a real DHT22 waveform, because that is the whole
 * justification for the primitive: a sensor whose bits are 26 versus 70
 * microseconds cannot be read by a protocol with a round trip per bit, so
 * "the host decodes the sensor" would otherwise be an empty claim.
 */

#include <string.h>
#include "unity.h"
#include "pulse.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "ag/protocol.h"
#include "ag/config.h"

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

static ag_status_t pulse_in(uint8_t pin, uint8_t level, uint32_t timeout)
{
    uint8_t p[6] = { pin, level, (uint8_t)timeout, (uint8_t)(timeout >> 8),
                     (uint8_t)(timeout >> 16), (uint8_t)(timeout >> 24) };
    return run(AG_CMD_PULSE_IN, p, 6u);
}

static ag_status_t capture(uint8_t pin, uint8_t start, uint8_t max, uint32_t timeout)
{
    uint8_t p[7] = { pin, start, max, (uint8_t)timeout, (uint8_t)(timeout >> 8),
                     (uint8_t)(timeout >> 16), (uint8_t)(timeout >> 24) };
    return run(AG_CMD_PULSE_CAPTURE, p, 7u);
}

static void test_pulse_in_returns_the_measured_width(void)
{
    ag_mock_set_pulse_in(1234u);
    TEST_ASSERT_EQUAL(AG_OK, pulse_in(2u, 1u, 50000u));
    uint32_t us = (uint32_t)outbuf[0] | ((uint32_t)outbuf[1] << 8)
                | ((uint32_t)outbuf[2] << 16) | ((uint32_t)outbuf[3] << 24);
    TEST_ASSERT_EQUAL_UINT32(1234u, us);
}

static void test_a_silent_line_times_out(void)
{
    ag_mock_set_pulse_in(0u);
    TEST_ASSERT_EQUAL(AG_ERR_TIMEOUT, pulse_in(2u, 1u, 1000u));
}

/* A pulse longer than the caller's own timeout is a timeout, not a reading.
 * Returning the width anyway would report a measurement the caller had
 * already decided it could not wait for. */
static void test_a_pulse_longer_than_the_timeout_is_a_timeout(void)
{
    ag_mock_set_pulse_in(9000u);
    TEST_ASSERT_EQUAL(AG_ERR_TIMEOUT, pulse_in(2u, 1u, 5000u));
}

/* No single command may hold the main loop past the cap, or a scheduled pin
 * misses its deadline. */
static void test_a_timeout_past_the_cap_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, pulse_in(2u, 1u, 5000000u));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, pulse_in(2u, 1u, 0u));
}

/*
 * THE DHT22 CASE, which is why this primitive exists.
 *
 * A DHT22 sends 40 bits as pulses of about 26 us for a zero and 70 us for a
 * one. The device captures intervals and knows nothing about humidity; the
 * host turns them into bits. The same command reads HC-SR04, IR and RC PPM.
 */
static void test_capture_returns_the_intervals_a_host_turns_into_bits(void)
{
    const uint16_t dht22[8] = { 26u, 70u, 26u, 26u, 70u, 70u, 26u, 70u };
    ag_mock_set_capture(dht22, 8u);

    TEST_ASSERT_EQUAL(AG_OK, capture(9u, 1u, 8u, 50000u));
    TEST_ASSERT_EQUAL_UINT8(8u, outbuf[0]);
    for (uint8_t i = 0u; i < 8u; i++) {
        uint16_t us = (uint16_t)((uint16_t)outbuf[1u + i * 2u]
                                 | ((uint16_t)outbuf[2u + i * 2u] << 8));
        TEST_ASSERT_EQUAL_UINT16(dht22[i], us);
    }
    /* and the host's decode: short is 0, long is 1 */
    uint8_t bits = 0u;
    for (uint8_t i = 0u; i < 8u; i++) {
        uint16_t us = (uint16_t)((uint16_t)outbuf[1u + i * 2u]
                                 | ((uint16_t)outbuf[2u + i * 2u] << 8));
        bits = (uint8_t)(((unsigned)bits << 1) | (us > 48u ? 1u : 0u));
    }
    TEST_ASSERT_EQUAL_HEX8(0x4Du, bits);   /* 0100 1101 */
}

/* A capture that ends early because the line went quiet returns what it has.
 * A sensor that stops after forty bits has said everything it had to say. */
static void test_a_short_capture_returns_what_it_got(void)
{
    const uint16_t few[3] = { 10u, 20u, 30u };
    ag_mock_set_capture(few, 3u);
    TEST_ASSERT_EQUAL(AG_OK, capture(9u, 1u, 20u, 50000u));
    TEST_ASSERT_EQUAL_UINT8(3u, outbuf[0]);
}

static void test_a_capture_with_no_edges_times_out(void)
{
    ag_mock_set_capture(0, 0u);
    TEST_ASSERT_EQUAL(AG_ERR_TIMEOUT, capture(9u, 1u, 20u, 50000u));
}

/* More edges than the reply can carry is refused rather than truncated: work
 * done and then thrown away is worse than a clear refusal. */
static void test_more_edges_than_a_frame_can_carry_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, capture(9u, 1u, 200u, 50000u));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, capture(9u, 1u, 0u,   50000u));
}

static void test_capture_on_a_pin_beyond_the_board_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_BAD_PIN, capture(99u, 1u, 8u, 50000u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pulse_in_returns_the_measured_width);
    RUN_TEST(test_a_silent_line_times_out);
    RUN_TEST(test_a_pulse_longer_than_the_timeout_is_a_timeout);
    RUN_TEST(test_a_timeout_past_the_cap_is_refused);
    RUN_TEST(test_capture_returns_the_intervals_a_host_turns_into_bits);
    RUN_TEST(test_a_short_capture_returns_what_it_got);
    RUN_TEST(test_a_capture_with_no_edges_times_out);
    RUN_TEST(test_more_edges_than_a_frame_can_carry_is_refused);
    RUN_TEST(test_capture_on_a_pin_beyond_the_board_is_refused);
    return UNITY_END();
}
