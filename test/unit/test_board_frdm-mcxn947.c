/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The FRDM-MCXN947 board's pin table, checked on x86.
 *
 * The table is the RGB LED (P0_10, P0_27, P1_2) and the two user switches
 * (SW2 P0_23, SW3 P0_6), as UM12018 and the SDK's board.h place them, then
 * the Arduino header's A2..A4, with A5 on SW2's pin, on LPADC0. No pin offers
 * a compare unit, since this port drives none.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_board_is_the_registered_mcxn947_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x46u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("frdm-mcxn947", tfw_board_name());
}

static void test_the_pins_are_the_rgb_led_and_the_switches(void)
{
    static const struct { uint8_t port, bit, led; } want[] = {
        { 0u, 10u, 1u },   /* LED_RED   */
        { 0u, 27u, 1u },   /* LED_GREEN */
        { 1u,  2u, 1u },   /* LED_BLUE  */
        { 0u, 23u, 0u },   /* SW2       */
        { 0u,  6u, 0u },   /* SW3       */
        { 0u, 14u, 0u },   /* A2        */
        { 0u, 22u, 0u },   /* A3        */
        { 0u, 15u, 0u },   /* A4        */
    };

    TEST_ASSERT_EQUAL_UINT8(sizeof want / sizeof want[0], TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(want[i].port, r.port);
        TEST_ASSERT_EQUAL_UINT8(want[i].bit, r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        TEST_ASSERT_EQUAL(want[i].led != 0u, (tfw_board_pin_flags(i) & TFW_PIN_LED) != 0u);
    }
}

/* A2 ADC0_B14, A3 ADC0_A14, A4 ADC0_B15, A5 (SW2's pin) ADC0_A15, as
 * docs/reference/README.md pins them: the channel, plus 32 for side B. */
static void test_the_adc_column_is_the_header_analog_pins(void)
{
    static const uint8_t want[] = {
        TFW_ADC_NONE, TFW_ADC_NONE, TFW_ADC_NONE, 15u, TFW_ADC_NONE, 46u, 14u, 47u,
    };

    TEST_ASSERT_EQUAL_UINT8(sizeof want, TFW_PIN_COUNT);
    TEST_ASSERT_EQUAL_UINT32(64u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL(1, TFW_FEATURE_ANALOG);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch = 0u;

        if (want[i] == TFW_ADC_NONE) {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        } else {
            TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(i, &ch));
            TEST_ASSERT_EQUAL_UINT8(want[i], ch);
            TEST_ASSERT_LESS_THAN_UINT32(TFW_ADC_COUNT, ch);
            TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        }
    }
}

static void test_only_the_red_led_offers_a_pwm(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;

        if (i == 0u) {
            continue;
        }
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
    }
}

/* Pin 0, the red LED on P0_10, is the one PWM row: CTIMER0 match 0
 * (CT0_MAT0). The row's fields are the target's tfw_pwmref_t, not the host
 * mock's, so the lookup and the flag are what is checked here. */
static void test_the_red_led_is_ctimer0_match0(void)
{
    tfw_pwmref_t p;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pwm(0u, &p));
    TEST_ASSERT_TRUE(tfw_board_pin_flags(0u) & TFW_PIN_PWM);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_PWM_COUNT);
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
}

/* The host link's pins are not in the table, so no command can drive them. */
static void test_the_link_pins_are_not_board_pins(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        if (r.port == TFW_MCXN_LINK_PORT) {
            TEST_ASSERT_NOT_EQUAL(TFW_MCXN_LINK_RX_BIT, r.bit);
            TEST_ASSERT_NOT_EQUAL(TFW_MCXN_LINK_TX_BIT, r.bit);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_the_registered_mcxn947_id);
    RUN_TEST(test_the_pins_are_the_rgb_led_and_the_switches);
    RUN_TEST(test_the_adc_column_is_the_header_analog_pins);
    RUN_TEST(test_only_the_red_led_offers_a_pwm);
    RUN_TEST(test_the_red_led_is_ctimer0_match0);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_the_link_pins_are_not_board_pins);
    return UNITY_END();
}
