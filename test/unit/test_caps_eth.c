/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

/* The CAPS handlers on an ETH=1 image's pin table. The shield's SPI and select
 * pins, D10-D13, are TFW_PIN_IN_USE there, and the 328P's one tone pin, D11
 * (OC2A), is the shield's MOSI. Tone must refuse it as reserved before it
 * touches the timer: a tone started on MOSI would clock noise into the W5x00. */

#include <string.h>
#include "unity.h"
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

static void test_the_build_reserves_the_tone_pin_for_the_shield(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, TFW_FEATURE_ETHERNET);
    TEST_ASSERT_TRUE((tfw_board_pin_flags(TFW_TONE_PIN) & TFW_PIN_IN_USE) != 0u);
}

static void test_tone_on_the_shields_mosi_is_refused_and_drives_nothing(void)
{
    const uint8_t on[3] = { TFW_TONE_PIN, 0x10u, 0x27u };   /* 10000 Hz */

    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_TONE, on, 3u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PINB, 3u));   /* D11 = PB3 */
    TEST_ASSERT_EQUAL_UINT(0u, tfw_writer_len(&w));
}

static void test_pwm_on_the_shields_pins_is_refused_and_drives_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 10u, 64u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PINB, 2u));   /* D10 = PB2 */
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 11u, 64u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PINB, 3u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_writer_len(&w));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_build_reserves_the_tone_pin_for_the_shield);
    RUN_TEST(test_tone_on_the_shields_mosi_is_refused_and_drives_nothing);
    RUN_TEST(test_pwm_on_the_shields_pins_is_refused_and_drives_nothing);
    return UNITY_END();
}
