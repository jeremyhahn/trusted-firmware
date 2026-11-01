/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

/*
 * ANALOG_READ and ADC_REF, through the dispatcher, under every request byte,
 * every value and every length (#163). The expected answer for each pin is a
 * literal taken from the 328P pin table, not computed from the lookup it
 * checks: A0..A7 are pins 14..21 on channels 0..7, every other pin below 22
 * has no converter, and everything from 22 up is beyond the board.
 *
 * "Never read" is observed by failing every conversion: a refused pin must
 * still answer its own refusal, not the converter's TFW_ERR_TIMEOUT, and must
 * write nothing.
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "analog.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

#define PIN_COUNT_328P 22u
#define FIRST_ADC_PIN  14u

static uint8_t      inbuf[TFW_MAX_PAYLOAD + 1u];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_mock_reset(); }
void tearDown(void) {}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    memset(outbuf, 0xA5, sizeof outbuf);
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

/* The 328P table's answer for pin p, as a literal. */
static tfw_status_t expected_for(uint8_t pin, uint8_t *ch)
{
    if (pin >= PIN_COUNT_328P) {
        return TFW_ERR_BAD_PIN;
    }
    if (pin < FIRST_ADC_PIN) {
        return TFW_ERR_UNSUPPORTED;
    }
    *ch = (uint8_t)(pin - FIRST_ADC_PIN);
    return TFW_OK;
}

static uint16_t reply_u16(void)
{
    return (uint16_t)(outbuf[0] | (outbuf[1] << 8));
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

/* Every request byte: a valid pin answers its own channel's value in two
 * bytes, an invalid one its refusal with nothing written. */
static void test_every_pin_byte_answers_its_table_row(void)
{
    char msg[48];

    for (uint8_t c = 0u; c < 8u; c++) {
        tfw_mock_set_adc(c, (uint16_t)(0x0300u + c * 0x11u));
    }
    for (unsigned b = 0u; b < 256u; b++) {
        uint8_t      pin = (uint8_t)b;
        uint8_t      ch  = 0xFFu;
        tfw_status_t exp = expected_for(pin, &ch);

        snprintf(msg, sizeof msg, "pin %u", b);
        TEST_ASSERT_EQUAL_MESSAGE(exp, run(TFW_CMD_ANALOG_READ, &pin, 1u), msg);
        if (exp == TFW_OK) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, tfw_writer_len(&w), msg);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)(0x0300u + ch * 0x11u), reply_u16(), msg);
        } else {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
        }
    }
}

/* A refused pin is refused before the converter: with every conversion failing,
 * it still answers its own status, not the converter's. A valid pin answers the
 * converter's status, and no reading goes out -- a zero in its place would read
 * as a dry sensor. */
static void test_a_refused_pin_never_reaches_the_converter(void)
{
    char msg[48];

    tfw_mock_adc_read_fails(1u);
    for (unsigned b = 0u; b < 256u; b++) {
        uint8_t      pin = (uint8_t)b;
        uint8_t      ch  = 0xFFu;
        tfw_status_t exp = expected_for(pin, &ch);

        snprintf(msg, sizeof msg, "pin %u", b);
        if (exp == TFW_OK) {
            exp = TFW_ERR_TIMEOUT;   /* a valid pin does reach it, and fails */
        }
        TEST_ASSERT_EQUAL_MESSAGE(exp, run(TFW_CMD_ANALOG_READ, &pin, 1u), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
    }
}

/* Every value the converter can hand back crosses the wire unchanged, on every
 * channel: not only the ten bits a 328P produces but the whole sixteen, so a
 * mask or a byte swap anywhere shows. */
static void test_every_value_on_every_channel_survives(void)
{
    char msg[48];

    for (uint8_t c = 0u; c < 8u; c++) {
        uint8_t pin = (uint8_t)(FIRST_ADC_PIN + c);

        for (uint32_t v = 0u; v <= 0xFFFFu; v++) {
            tfw_mock_set_adc(c, (uint16_t)v);
            if (run(TFW_CMD_ANALOG_READ, &pin, 1u) != TFW_OK
                || tfw_writer_len(&w) != 2u || reply_u16() != (uint16_t)v) {
                snprintf(msg, sizeof msg, "channel %u value %lu", c, (unsigned long)v);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

/* Every length other than one is refused by arity, whatever the bytes, and
 * nothing is written or read. */
static void test_every_other_length_is_refused(void)
{
    uint8_t payload[TFW_MAX_PAYLOAD + 1u];
    char    msg[48];

    tfw_mock_adc_read_fails(1u);
    for (unsigned fill = 0u; fill < 256u; fill += 17u) {
        memset(payload, (int)fill, sizeof payload);
        payload[0] = FIRST_ADC_PIN;   /* a valid pin first, so only the length is wrong */
        for (unsigned n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
            if (n == 1u) { continue; }
            snprintf(msg, sizeof msg, "fill %u length %u", fill, n);
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC,
                                      run(TFW_CMD_ANALOG_READ, payload, (uint8_t)n), msg);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC,
                                      run(TFW_CMD_ADC_REF, payload, (uint8_t)n), msg);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
        }
    }
}

/* Every reference byte: the three documented references are taken, every other
 * value is out of range, and neither answer writes a reply. */
static void test_every_reference_byte(void)
{
    char msg[48];

    for (unsigned b = 0u; b < 256u; b++) {
        uint8_t ref = (uint8_t)b;

        snprintf(msg, sizeof msg, "ref %u", b);
        TEST_ASSERT_EQUAL_MESSAGE(b <= 2u ? TFW_OK : TFW_ERR_RANGE,
                                  run(TFW_CMD_ADC_REF, &ref, 1u), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
    }
}

/* A refused reference leaves the converter usable: a read afterwards still
 * answers the value it was given. */
static void test_a_refused_reference_does_not_disturb_a_read(void)
{
    uint8_t pin = FIRST_ADC_PIN + 3u;

    tfw_mock_set_adc(3u, 0x0155u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_ADC_REF, (const uint8_t[]){ 3u }, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, &pin, 1u));
    TEST_ASSERT_EQUAL_UINT16(0x0155u, reply_u16());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reading_an_analog_pin_uses_the_right_channel);
    RUN_TEST(test_every_pin_byte_answers_its_table_row);
    RUN_TEST(test_a_refused_pin_never_reaches_the_converter);
    RUN_TEST(test_every_value_on_every_channel_survives);
    RUN_TEST(test_every_other_length_is_refused);
    RUN_TEST(test_every_reference_byte);
    RUN_TEST(test_a_refused_reference_does_not_disturb_a_read);
    return UNITY_END();
}
