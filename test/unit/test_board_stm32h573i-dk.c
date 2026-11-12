/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H573I-DK's pin table, checked on x86.
 *
 * Pins 0-3 are the kit's four user LEDs, at the banks and bits ST's BSP
 * names (stm32h573i_discovery.h): LD1 green PI9, LD2 orange PI8, LD3 red
 * PF1, LD4 blue PF4. Pins 4-9 are the Arduino header's A0 to A5 (Zephyr's
 * arduino_r3_connector.dtsi for the board) at their ADC1 inputs (hal_stm32's
 * stm32h573iikxq-pinctrl.dtsi): A0 PB0 INP9, A1 PA4 INP18, A2 PA0 INP0,
 * A3 PA5 INP19, A4 PA6 INP3, A5 PF12 INP6. Port 0 is GPIOF (bank 5), 1
 * GPIOI (8), 2 GPIOA (0), 3 GPIOB (1). No pin offers a compare unit.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_board_is_its_registered_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x43u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("stm32h573i-dk", tfw_board_name());
}

static void test_the_first_four_pins_are_the_leds_at_their_banks(void)
{
    static const struct { uint8_t bank; uint8_t bit; } want[4] = {
        { 8u, 9u },   /* LD1 PI9 */
        { 8u, 8u },   /* LD2 PI8 */
        { 5u, 1u },   /* LD3 PF1 */
        { 5u, 4u },   /* LD4 PF4 */
    };

    TEST_ASSERT_EQUAL_UINT8(10u, TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < 4u; i++) {
        tfw_pinref_t r;
        uint8_t      ch;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(want[i].bank, r.port);
        TEST_ASSERT_EQUAL_UINT8(want[i].bit, r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_LED);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
    }
}

static void test_a0_to_a5_are_their_adc1_inputs(void)
{
    static const struct { uint8_t bank; uint8_t bit; uint8_t inp; } want[6] = {
        { 1u,  0u,  9u },   /* A0 PB0  INP9  */
        { 0u,  4u, 18u },   /* A1 PA4  INP18 */
        { 0u,  0u,  0u },   /* A2 PA0  INP0  */
        { 0u,  5u, 19u },   /* A3 PA5  INP19 */
        { 0u,  6u,  3u },   /* A4 PA6  INP3  */
        { 5u, 12u,  6u },   /* A5 PF12 INP6  */
    };

    TEST_ASSERT_EQUAL_UINT8(20u, TFW_ADC_COUNT);
    for (uint8_t i = 0u; i < 6u; i++) {
        uint8_t      pin = (uint8_t)(4u + i);
        tfw_pinref_t r;
        uint8_t      ch = 0xEEu;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(pin, &r));
        TEST_ASSERT_EQUAL_UINT8(want[i].bank, r.port);
        TEST_ASSERT_EQUAL_UINT8(want[i].bit, r.bit);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(pin, &ch));
        TEST_ASSERT_EQUAL_UINT8(want[i].inp, ch);
        TEST_ASSERT_TRUE(ch < TFW_ADC_COUNT);
        TEST_ASSERT_EQUAL_HEX8(TFW_PIN_ADC, tfw_board_pin_flags(pin));
    }
}

static void test_no_pin_offers_a_pwm(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
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
    RUN_TEST(test_the_board_is_its_registered_id);
    RUN_TEST(test_the_first_four_pins_are_the_leds_at_their_banks);
    RUN_TEST(test_a0_to_a5_are_their_adc1_inputs);
    RUN_TEST(test_no_pin_offers_a_pwm);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    return UNITY_END();
}
