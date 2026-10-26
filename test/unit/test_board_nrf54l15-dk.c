/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L15 DK's pin table, checked on x86.
 *
 * The table is the DK's four LEDs and four buttons on the SoC's three GPIO
 * banks, and no pin offers a converter or a compare unit yet. The pins are
 * the board's (Zephyr's nrf54l15dk_common.dtsi); a slip here lights the wrong
 * LED on a board nobody is watching.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_board_is_its_registry_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x45u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("nrf54l15-dk", tfw_board_name());
}

static void test_the_pins_are_the_four_leds_then_the_four_buttons(void)
{
    static const uint8_t port[8] = { 2u, 1u, 2u, 1u, 1u, 1u, 1u, 0u };
    static const uint8_t bit[8]  = { 9u, 10u, 7u, 14u, 13u, 9u, 8u, 4u };

    TEST_ASSERT_EQUAL_UINT8(8u, TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(port[i], r.port);
        TEST_ASSERT_EQUAL_UINT8(bit[i], r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        if (i < 4u) {
            TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_LED);
        } else {
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_LED);
        }
    }
}

static void test_no_pin_is_the_console(void)
{
    /* P1.4 and P1.5 are the UARTE's; a pin table entry would let a host
     * command drive the console's own line. */
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_FALSE(r.port == 1u && (r.bit == 4u || r.bit == 5u));
    }
}

static void test_no_pin_offers_an_adc_or_a_pwm(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
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
    RUN_TEST(test_the_board_is_its_registry_id);
    RUN_TEST(test_the_pins_are_the_four_leds_then_the_four_buttons);
    RUN_TEST(test_no_pin_is_the_console);
    RUN_TEST(test_no_pin_offers_an_adc_or_a_pwm);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    return UNITY_END();
}
