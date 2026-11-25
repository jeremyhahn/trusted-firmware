/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Pico 2 W board's pin table, checked on x86.
 *
 * The pins are the header GPIOs on SIO's port 0, with the bit the GPIO
 * number: GPIO2-22, then GPIO26-28. The UART's GPIO0-1 and the radio's
 * GPIO23-25 and 29 are never pins, no pin is an LED (the Pico 2 W's is the
 * radio's). GP26-28 are ADC channels 0-2 and one pin per PWM slice offers a
 * compare unit; the PERIPH=0 row offers neither.
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

/* GP26-GP28 are ADC0-2 and no other pin has an ADC. The even GPIOs 2-16 offer PWM, one per slice (board.c;
 * the next test checks each row's fields, and test_rp2350_pwm.c checks
 * hal_pwm.c refuses a mismatched row);
 * no other pin does. */
#if TFW_FEATURE_PWM
static void test_adc_on_gp26_to_gp28_and_pwm_on_one_pin_per_slice(void)
{
    uint8_t row = 0u;

    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch = 0xFFu;
        tfw_pwmref_t p;
        tfw_pinref_t r;
        uint8_t gpio;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        gpio = r.bit;
        if (i >= 21u) {          /* pins 21-23 are GP26-GP28 */
            TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(i, &ch));
            TEST_ASSERT_EQUAL_UINT8(i - 21u, ch);
            TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        } else {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        }
        if (gpio >= 2u && gpio <= 16u && (gpio & 1u) == 0u) {
            TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pwm(i, &p));
            TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
            row++;
        } else {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
        }
    }
    TEST_ASSERT_EQUAL(TFW_PWM_COUNT, row);
    TEST_ASSERT_EQUAL_UINT32(3u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL_UINT32(0x1140u, TFW_EEPROM_SIZE);
    TEST_ASSERT_EQUAL_INT(1, TFW_FEATURE_ANALOG);
    TEST_ASSERT_EQUAL_INT(1, TFW_FEATURE_TONE);
    TEST_ASSERT_EQUAL_INT(1, TFW_FEATURE_EEPROM);
}

/* On the host a row's four fields land positionally in test/mock/hal_types.h's
 * ctl, duty, bit and wide as board.c's { slice, channel, port, GPIO }: each
 * row is its own GPIO's slice (n >> 1) & 7 and channel n & 1 on port 0, and
 * the eight rows are the eight slices, once each. */
static void test_each_pwm_row_is_its_own_gpios_slice_and_no_slice_is_shared(void)
{
    uint8_t seen = 0u;

    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;
        tfw_pinref_t r;
        uint8_t gpio;

        if (tfw_board_pwm(i, &p) != TFW_OK) {
            continue;
        }
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        gpio = r.bit;
        TEST_ASSERT_EQUAL_UINT8(gpio, p.wide);
        TEST_ASSERT_EQUAL_UINT16((gpio >> 1) & 7u, p.ctl);
        TEST_ASSERT_EQUAL_UINT16(gpio & 1u, p.duty);
        TEST_ASSERT_EQUAL_UINT8(0u, p.bit);
        TEST_ASSERT_EQUAL_HEX8(0u, seen & (1u << p.ctl));
        seen = (uint8_t)(seen | (1u << p.ctl));
    }
    TEST_ASSERT_EQUAL_HEX8(0xFFu, seen);
}
#else
/* The PERIPH=0 row is GPIO only: no converter input, no PWM row, no tone and
 * no EEPROM. The PERIPH=1 row (TFW_CM_PERIPH), the default since ruling 697
 * lifted the AVR gate (#7) for the RP2350, turns them on. */
static void test_the_periph_0_row_is_gpio_only(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch = 0xFFu;
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & (TFW_PIN_ADC | TFW_PIN_PWM));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_PWM_COUNT);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_EEPROM_SIZE);
    TEST_ASSERT_EQUAL_INT(0, TFW_FEATURE_ANALOG);
    TEST_ASSERT_EQUAL_INT(0, TFW_FEATURE_PWM);
    TEST_ASSERT_EQUAL_INT(0, TFW_FEATURE_TONE);
    TEST_ASSERT_EQUAL_INT(0, TFW_FEATURE_EEPROM);
}
#endif

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
}

/* GP4 (pin 2) and GP5 (pin 3) are I2C0's SDA and SCL in the I2C=1 row: taken,
 * so GPIO_MODE and PIN_SCHED_SET (src/caps/gpio.c, src/caps/sched.c) refuse
 * them with TFW_ERR_RESERVED_PIN instead of switching them to SIO. In the
 * I2C=0 row, and for every other pin, nothing is taken. */
static void test_the_i2c_row_takes_sda_and_scl_and_nothing_else(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t taken = (uint8_t)(tfw_board_pin_flags(i) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE));

        if ((TFW_FEATURE_I2C) && (i == 2u || i == 3u)) {
            TEST_ASSERT_EQUAL_HEX8(TFW_PIN_IN_USE, taken);
        } else {
            TEST_ASSERT_EQUAL_HEX8(0u, taken);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_its_registry_id);
    RUN_TEST(test_the_pins_are_the_header_gpios_in_order);
    RUN_TEST(test_no_pin_is_the_uart_or_the_radio);
#if TFW_FEATURE_PWM
    RUN_TEST(test_adc_on_gp26_to_gp28_and_pwm_on_one_pin_per_slice);
    RUN_TEST(test_each_pwm_row_is_its_own_gpios_slice_and_no_slice_is_shared);
#else
    RUN_TEST(test_the_periph_0_row_is_gpio_only);
#endif
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_the_i2c_row_takes_sda_and_scl_and_nothing_else);
    return UNITY_END();
}
