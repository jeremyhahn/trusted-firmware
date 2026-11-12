/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The FRDM-IMX95-PRO M7 board's pin table, checked on x86.
 *
 * The table is two provisional RGPIO lines, GPIO2_IO00 and GPIO2_IO01 (port
 * 1, bits 0 and 1), digital and nothing else: no pin is claimed as an LED,
 * and none offers a converter or a compare unit (boards/frdm-imx95-m7/
 * board.c).
 *
 * Runs under `make board-sweep`, against this board's own profile, and in
 * `make host-test` as test_board_frdm-imx95-m7_adc, the soft:ADC=1 row
 * (TFW_CM_ADC=1), where the analog pins' assertions run.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_board_is_the_next_provisional_cortex_m_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x41u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("frdm-imx95-m7", tfw_board_name());
}

static void test_the_pins_are_two_gpio2_lines(void)
{
    TEST_ASSERT_EQUAL_UINT8(TFW_CM_ADC ? 4u : 2u, TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < 2u; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(1u, r.port);
        TEST_ASSERT_EQUAL_UINT8(i, r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_LED);
    }
}

#if TFW_CM_ADC
/* The soft:ADC=1 row: the SAR ADC's inputs appended after the GPIO pins, ADC
 * only, one channel each, and the count of channels to match. */
static void test_the_analog_pins_name_their_channels(void)
{
    TEST_ASSERT_EQUAL_UINT8(2u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL_INT(1, TFW_FEATURE_ANALOG);
    for (uint8_t i = 2u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch = 0xFFu;
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_EQUAL_UINT8(i - 2u, ch);
        TEST_ASSERT_TRUE(ch < TFW_ADC_COUNT);
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_EQUAL_HEX8(TFW_PIN_ADC, tfw_board_pin_flags(i));
    }
}
#else
static void test_the_row_off_has_no_channel(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL_INT(0, TFW_FEATURE_ANALOG);
}
#endif

static void test_no_pin_offers_an_adc_or_a_pwm(void)
{
    for (uint8_t i = 0u; i < 2u; i++) {
        uint8_t ch;
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & (TFW_PIN_ADC | TFW_PIN_PWM));
    }
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_the_next_provisional_cortex_m_id);
    RUN_TEST(test_the_pins_are_two_gpio2_lines);
    RUN_TEST(test_no_pin_offers_an_adc_or_a_pwm);
#if TFW_CM_ADC
    RUN_TEST(test_the_analog_pins_name_their_channels);
#else
    RUN_TEST(test_the_row_off_has_no_channel);
#endif
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    return UNITY_END();
}
