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
#include "tfw/hal.h"

/* The board sweep links only the board table and its lookup; the ANALOG_READ
 * handler and the codec it reads and writes with come in as sources. */
#include "../../src/caps/analog.c"
#include "../../src/core/reader.c"
#include "../../src/core/writer.c"

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

    TEST_ASSERT_EQUAL_UINT8(12u, TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < 8u; i++) {
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

static void test_no_led_or_button_offers_an_adc_or_a_pwm(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch;
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
        if (i < 8u) {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        }
    }
}

/* Pins 8-11 are AIN2-AIN5 on P1.6, P1.7, P1.11 and P1.12, the SAADC channels
 * 0-3 of src/hal/cortex-m/nrf54l/soc_tables.c. */
static void test_the_ain_pins_are_adc_channels_0_to_3(void)
{
    static const uint8_t bit[4] = { 6u, 7u, 11u, 12u };

    for (uint8_t i = 0u; i < 4u; i++) {
        tfw_pin_t    pin = (tfw_pin_t)(8u + i);
        tfw_pinref_t r;
        uint8_t      ch = 0xFFu;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(pin, &r));
        TEST_ASSERT_EQUAL_UINT8(1u, r.port);
        TEST_ASSERT_EQUAL_UINT8(bit[i], r.bit);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(pin, &ch));
        TEST_ASSERT_EQUAL_UINT8(i, ch);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(pin) & TFW_PIN_ADC);
        TEST_ASSERT_FALSE(tfw_board_pin_flags(pin) & TFW_PIN_LED);
    }
}

static uint8_t hal_calls;
static uint8_t hal_channel;

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    hal_calls++;
    hal_channel = channel;
    *out_raw = (uint16_t)(0x0100u + channel);
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    (void)ref;
    return TFW_OK;
}

static tfw_status_t analog_read(uint8_t pin, uint8_t out[2], uint8_t *len)
{
    uint8_t      in[1] = { pin };
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    tfw_reader_init(&r, in, 1u);
    tfw_writer_init(&w, out, 2u);
    st = tfw_analog_cmd_read(&r, &w);
    *len = w.pos;
    return st;
}

/* ANALOG_READ on pins 8-11 reaches the HAL with channels 0-3 and answers its
 * reading; on the LEDs and buttons it stops at the board table. */
static void test_analog_read_reaches_the_saadc_only_on_the_ain_pins(void)
{
    for (uint8_t pin = 0u; pin < TFW_PIN_COUNT; pin++) {
        uint8_t out[2] = { 0u, 0u };
        uint8_t len;

        hal_calls = 0u;
        if (pin < 8u) {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, analog_read(pin, out, &len));
            TEST_ASSERT_EQUAL_UINT8(0u, hal_calls);
            TEST_ASSERT_EQUAL_UINT8(0u, len);
        } else {
            uint8_t ch = (uint8_t)(pin - 8u);

            TEST_ASSERT_EQUAL(TFW_OK, analog_read(pin, out, &len));
            TEST_ASSERT_EQUAL_UINT8(1u, hal_calls);
            TEST_ASSERT_EQUAL_UINT8(ch, hal_channel);
            TEST_ASSERT_EQUAL_UINT8(2u, len);
            TEST_ASSERT_EQUAL_UINT8(ch, out[0]);
            TEST_ASSERT_EQUAL_UINT8(0x01u, out[1]);
        }
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
    RUN_TEST(test_no_led_or_button_offers_an_adc_or_a_pwm);
    RUN_TEST(test_the_ain_pins_are_adc_channels_0_to_3);
    RUN_TEST(test_analog_read_reaches_the_saadc_only_on_the_ain_pins);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    return UNITY_END();
}
