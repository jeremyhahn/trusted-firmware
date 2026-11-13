/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The NUCLEO-H563ZI's pin table and console, checked on x86.
 *
 * The pins are the board's three user LEDs (UM3115, TO CONFIRM): LD1 green
 * PB0, LD2 yellow PF4, LD3 red PG4. Port 0 is GPIOB, port 1 GPIOF and port 2
 * GPIOG, and no pin offers a converter or a compare unit. The console is
 * USART3 on PD8/PD9 (src/hal/cortex-m/stm32h5/soc_console.h), which the
 * board's profile selects.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"
#include "hal/cortex-m/stm32h5/soc_console.h"

#define STR_(x) #x
#define STR(x)  STR_(x)

void setUp(void) {}
void tearDown(void) {}

static void test_the_board_is_its_registered_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x47u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("nucleo-h563zi", tfw_board_name());
}

static void test_the_pins_are_the_three_leds_at_their_banks(void)
{
    static const struct { uint8_t bank; uint8_t bit; } want[3] = {
        { 1u, 0u },   /* LD1 PB0 */
        { 5u, 4u },   /* LD2 PF4 */
        { 6u, 4u },   /* LD3 PG4 */
    };

    TEST_ASSERT_EQUAL_UINT8(3u, TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(want[i].bank, r.port);
        TEST_ASSERT_EQUAL_UINT8(want[i].bit, r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_LED);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
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

static void test_the_console_is_usart3_on_pd8_pd9(void)
{
    TEST_ASSERT_EQUAL_INT(1, TFW_H5_CONSOLE_USART3);
    TEST_ASSERT_EQUAL_HEX32(0x40004800u, TFW_H5_CON_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x9Cu, TFW_H5_CON_ENR_OFF);
    TEST_ASSERT_EQUAL_HEX32(1u << 18, TFW_H5_CON_EN);
    TEST_ASSERT_EQUAL_UINT32(60u, TFW_H5_CON_IRQ);
    TEST_ASSERT_EQUAL_STRING("tfw_cm_irq60", STR(TFW_H5_CON_HANDLER));
    TEST_ASSERT_EQUAL_UINT32(3u, TFW_H5_CON_BANK);
    TEST_ASSERT_EQUAL_UINT32(8u, TFW_H5_CON_TX_PIN);
    TEST_ASSERT_EQUAL_UINT32(9u, TFW_H5_CON_RX_PIN);
    TEST_ASSERT_EQUAL_UINT32(7u, TFW_H5_CON_AF);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_its_registered_id);
    RUN_TEST(test_the_pins_are_the_three_leds_at_their_banks);
    RUN_TEST(test_no_pin_offers_an_adc_or_a_pwm);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_the_console_is_usart3_on_pd8_pd9);
    return UNITY_END();
}
