/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H573I-DK's pin table, checked on x86.
 *
 * The pins are the kit's four user LEDs, at the banks and bits ST's BSP
 * names (stm32h573i_discovery.h): LD1 green PI9, LD2 orange PI8, LD3 red
 * PF1, LD4 blue PF4. Port 0 is GPIOF (bank 5) and port 1 GPIOI (bank 8), and
 * no pin offers a converter or a compare unit.
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

static void test_the_pins_are_the_leds_then_d6_and_d5_at_their_banks(void)
{
    static const struct { uint8_t bank; uint8_t bit; uint8_t led; } want[6] = {
        { 8u, 9u, 1u },    /* LD1 PI9 */
        { 8u, 8u, 1u },    /* LD2 PI8 */
        { 5u, 1u, 1u },    /* LD3 PF1 */
        { 5u, 4u, 1u },    /* LD4 PF4 */
        { 7u, 10u, 0u },   /* ARD_D6 PH10 */
        { 7u, 11u, 0u },   /* ARD_D5 PH11 */
    };

    TEST_ASSERT_EQUAL_UINT8(6u, TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(want[i].bank, r.port);
        TEST_ASSERT_EQUAL_UINT8(want[i].bit, r.bit);
        TEST_ASSERT_EQUAL(want[i].led != 0u, (tfw_board_pin_flags(i) & TFW_PIN_LED) != 0u);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
    }
}

static void test_only_d6_and_d5_offer_a_pwm_and_no_pin_an_adc(void)
{
    /* The rows' TIM5 fields are the target's tfw_pwmref_t, which the host's
     * (test/mock/hal_types.h) does not share: here, that D6 and D5 have a
     * row and nothing else does. */
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch;
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        if (i < 4u) {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
            continue;
        }
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pwm(i, &p));
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
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
    RUN_TEST(test_the_pins_are_the_leds_then_d6_and_d5_at_their_banks);
    RUN_TEST(test_only_d6_and_d5_offer_a_pwm_and_no_pin_an_adc);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    return UNITY_END();
}
