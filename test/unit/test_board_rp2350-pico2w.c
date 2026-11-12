/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Pico 2 W board's pin table, checked on x86.
 *
 * The pins are the header GPIOs on SIO's port 0, with the bit the GPIO
 * number: GPIO2-22, then GPIO26-28. The UART's GPIO0-1 and the radio's
 * GPIO23-25 and 29 are never pins, no pin is an LED (the Pico 2 W's is the
 * radio's), and no pin offers a converter or a compare unit yet.
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
    TEST_ASSERT_EQUAL_HEX8(0x44u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("rp2350-pico2w", tfw_board_name());
}

static void test_the_pins_are_the_header_gpios_in_order(void)
{
    static const uint8_t gpio[24] = {
        2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u,
        16u, 17u, 18u, 19u, 20u, 21u, 22u, 26u, 27u, 28u,
    };

    TEST_ASSERT_EQUAL_UINT8(24u, TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(0u, r.port);
        TEST_ASSERT_EQUAL_UINT8(gpio[i], r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_LED);
    }
}

static void test_no_pin_is_the_uart_or_the_radio(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_TRUE(r.bit >= 2u);
        TEST_ASSERT_TRUE(r.bit < 23u || (r.bit > 25u && r.bit < 29u));
    }
}

static void test_gp26_to_gp28_are_adc0_to_adc2_and_no_pin_offers_a_pwm(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch = 0xFFu;
        tfw_pwmref_t p;

        if (i >= 21u) {          /* pins 21-23 are GP26-GP28 */
            TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(i, &ch));
            TEST_ASSERT_EQUAL_UINT8(i - 21u, ch);
            TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        } else {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        }
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
    }
    TEST_ASSERT_EQUAL_UINT32(3u, TFW_ADC_COUNT);
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
    RUN_TEST(test_the_pins_are_the_header_gpios_in_order);
    RUN_TEST(test_no_pin_is_the_uart_or_the_radio);
    RUN_TEST(test_gp26_to_gp28_are_adc0_to_adc2_and_no_pin_offers_a_pwm);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    return UNITY_END();
}
