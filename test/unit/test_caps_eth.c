/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

/* The CAPS handlers on an ETH=1 image's pin table. The shield's SPI and select
 * pins, D10-D13, are TFW_PIN_IN_USE there, and the 328P's one tone pin, D11
 * (OC2A), is the shield's MOSI. Tone must refuse it as reserved before it
 * touches the timer: a tone started on MOSI would clock noise into the W5x00.
 * PWM, PIN_MODE and DIGITAL_WRITE refuse all four pins too, and SPI_XFER
 * refuses D10 as a chip-select, so the host cannot drive the shield's bus;
 * DIGITAL_READ still answers on them. */

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

/* The pin check comes before the reservation check: the shield's SCK is not
 * the tone pin on any build, so it is unsupported, not reserved. */
static void test_tone_on_the_shields_sck_is_unsupported_and_drives_nothing(void)
{
    const uint8_t on[3] = { 13u, 0x10u, 0x27u };             /* D13, 10000 Hz */

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run(TFW_CMD_TONE, on, 3u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PINB, 5u));   /* D13 = PB5 */
    TEST_ASSERT_EQUAL_UINT(0u, tfw_writer_len(&w));
}

/* The reservation is checked before the frequency reaches the timer: a pitch
 * below the reachable range on the shield's MOSI is reserved, not out of range. */
static void test_a_low_tone_on_the_shields_mosi_is_reserved_not_out_of_range(void)
{
    const uint8_t low[3] = { TFW_TONE_PIN, 100u, 0u };       /* 100 Hz */

    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_TONE, low, 3u));
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

/* D10-D13 are PB2-PB5. Neither PIN_MODE nor DIGITAL_WRITE may turn one into an
 * output or drive it: the W5x00 owns the select and the SPI lines. */
static void test_pin_mode_and_write_on_the_shields_pins_are_refused_and_drive_nothing(void)
{
    uint8_t ran = 0u;

    for (uint8_t pin = 10u; pin <= 13u; pin++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN,
                          run(TFW_CMD_PIN_MODE, (const uint8_t[]){ pin, TFW_PIN_OUTPUT }, 2u));
        TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN,
                          run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ pin, 1u }, 2u));
        ran++;
    }
    TEST_ASSERT_EQUAL_UINT(4u, ran);
    for (uint8_t bit = 2u; bit <= 5u; bit++) {
        TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PINB, bit));
        TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_pin_level(TFW_ADDR_PINB, bit));
    }
    TEST_ASSERT_EQUAL_UINT(0u, tfw_writer_len(&w));
}

/* Reading a reserved pin stays allowed (gpio.c's DIGITAL_READ), so the refusals
 * above cannot be passed by a handler that refuses every shield pin. */
static void test_digital_read_on_the_shields_miso_still_answers(void)
{
    tfw_mock_set_input_level(TFW_ADDR_PINB, 4u, 1u);             /* D12 = PB4 */
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ 12u }, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT8(1u, outbuf[0]);
}

/* test_bus.c makes the same call succeed on the serial pin table, where D10 is
 * free. Here D10 is the W5x00's select: the transfer is refused before the bus
 * is touched, so the controller's registers cannot be rewritten. */
static void test_an_spi_transfer_selecting_the_shield_is_refused(void)
{
    memset(outbuf, 0, sizeof outbuf);
    tfw_mock_set_bus_response((const uint8_t[]){ 0x12u, 0x34u }, 2u);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION,
                      run(TFW_CMD_SPI_XFER, (const uint8_t[]){ 10u, 0xAAu, 0xBBu }, 3u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_writer_len(&w));
    TEST_ASSERT_NOT_EQUAL(0x12u, outbuf[0]);
    TEST_ASSERT_NOT_EQUAL(0x34u, outbuf[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_build_reserves_the_tone_pin_for_the_shield);
    RUN_TEST(test_tone_on_the_shields_mosi_is_refused_and_drives_nothing);
    RUN_TEST(test_tone_on_the_shields_sck_is_unsupported_and_drives_nothing);
    RUN_TEST(test_a_low_tone_on_the_shields_mosi_is_reserved_not_out_of_range);
    RUN_TEST(test_pwm_on_the_shields_pins_is_refused_and_drives_nothing);
    RUN_TEST(test_pin_mode_and_write_on_the_shields_pins_are_refused_and_drive_nothing);
    RUN_TEST(test_digital_read_on_the_shields_miso_still_answers);
    RUN_TEST(test_an_spi_transfer_selecting_the_shield_is_refused);
    return UNITY_END();
}
