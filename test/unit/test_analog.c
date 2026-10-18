/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "unity.h"
#include "analog.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

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

/* A0 is logical pin 14 and ADC channel 0. Going through the board descriptor
 * means a wrong channel mapping fails here rather than on a bench. */
static void test_reading_an_analog_pin_uses_the_right_channel(void)
{
    tfw_mock_set_adc(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 14u }, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, outbuf[0]);   /* little-endian 512 */
    TEST_ASSERT_EQUAL_HEX8(0x02u, outbuf[1]);
}

/* The full ten bits must survive. A converter quietly truncated to eight would
 * still look plausible in every reading below 256. */
static void test_the_full_ten_bit_range_survives(void)
{
    tfw_mock_set_adc(1u, 1023u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 15u }, 1u));
    TEST_ASSERT_EQUAL_UINT16(1023u, (uint16_t)(outbuf[0] | (outbuf[1] << 8)));
}

static void test_each_analog_pin_reads_its_own_channel(void)
{
    for (uint8_t i = 0u; i < 6u; i++) {
        tfw_mock_set_adc(i, (uint16_t)(100u + i));
    }
    for (uint8_t i = 0u; i < 6u; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ (uint8_t)(14u + i) }, 1u));
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(100u + i), (uint16_t)(outbuf[0] | (outbuf[1] << 8)));
    }
}

/* The ADC-only pins on the TQFP package are readable even though they cannot
 * be driven -- which is the whole reason they are in the table. */
static void test_the_adc_only_pins_are_readable(void)
{
    tfw_mock_set_adc(6u, 777u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 20u }, 1u));
    TEST_ASSERT_EQUAL_UINT16(777u, (uint16_t)(outbuf[0] | (outbuf[1] << 8)));
}

static void test_a_digital_only_pin_has_no_analog_reading(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 13u }, 1u));
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 99u }, 1u));
}

/* A conversion the converter refuses is the converter's status, and no reading
 * goes out -- a zero in its place would read as a dry sensor. */
static void test_a_failed_conversion_returns_its_status_and_writes_nothing(void)
{
    tfw_mock_set_adc(0u, 512u);
    tfw_mock_adc_read_fails(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, run(TFW_CMD_ANALOG_READ, (const uint8_t[]){ 14u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_the_reference_can_be_selected_and_is_range_checked(void)
{
    for (uint8_t ref = 0u; ref <= 2u; ref++) {
        TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ADC_REF, &ref, 1u));
    }
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_ADC_REF, (const uint8_t[]){ 9u }, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reading_an_analog_pin_uses_the_right_channel);
    RUN_TEST(test_the_full_ten_bit_range_survives);
    RUN_TEST(test_each_analog_pin_reads_its_own_channel);
    RUN_TEST(test_the_adc_only_pins_are_readable);
    RUN_TEST(test_a_digital_only_pin_has_no_analog_reading);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_a_failed_conversion_returns_its_status_and_writes_nothing);
    RUN_TEST(test_the_reference_can_be_selected_and_is_range_checked);
    return UNITY_END();
}
