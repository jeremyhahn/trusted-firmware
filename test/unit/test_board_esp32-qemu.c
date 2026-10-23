/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The QEMU board's pin table, checked on x86.
 *
 * One table for two emulated machines, so the property that matters is that
 * every pin exists on BOTH and is spent on neither's flash or console: the
 * original ESP32 gives GPIO 6..11 to its flash and 1/3 to UART0, the C3 has
 * no GPIO above 21, gives 12..17 to its flash and 20/21 to UART0.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include <stdio.h>
#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"
#include "tfw/boot.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t gpio(tfw_pin_t pin)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(pin, &r));
    return (uint8_t)(r.port + r.bit);
}

static void test_the_board_is_the_second_esp32_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x81u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("esp32-qemu", tfw_board_name());
}

static void test_the_pins_are_gpio_2_4_5_18_19(void)
{
    static const uint8_t want[TFW_PIN_COUNT] = { 2, 4, 5, 18, 19 };

    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT8(want[i], gpio(i));
    }
}

static void test_every_pin_exists_on_both_machines_and_is_free_on_both(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t g = gpio(i);

        TEST_ASSERT_LESS_OR_EQUAL_UINT8(19u, g);       /* the C3 ends at 21; 20/21 are its UART0 */
        TEST_ASSERT_FALSE(g >= 12u && g <= 17u);       /* the C3's flash */
        TEST_ASSERT_FALSE(g >= 6u && g <= 11u);        /* the ESP32's flash */
        TEST_ASSERT_FALSE(g == 1u || g == 3u);         /* the ESP32's UART0 */
    }
}

static void test_no_pin_offers_an_adc(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
    }
}

static void test_every_pwm_row_drives_the_pin_that_names_it(void)
{
    uint8_t n = 0u;

    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;

        if (tfw_board_pwm(i, &p) == TFW_OK) {
            TEST_ASSERT_EQUAL_UINT16(gpio(i), p.duty);
            TEST_ASSERT_EQUAL_UINT16(n, p.ctl);
            n++;
        }
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_PWM_COUNT, n);
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
}

/* Literals, as test_boot_geometry.c's rows: layer 0's gate and the app's
 * OTA_ACTIVATE check parse the trailer with this bound. config.h's default,
 * 28 544, refused every ESP-IDF app at OTA_ACTIVATE with TFW_ERR_RANGE. */
static void test_the_trailer_bound_is_16_mib_less_the_trailer(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_BOOT_APP_BASE);
    TEST_ASSERT_EQUAL_UINT32(16777216u, TFW_BOOT_APP_END);
    TEST_ASSERT_EQUAL_UINT32(16777088u, TFW_BOOT_MAX_IMAGE_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_the_second_esp32_id);
    RUN_TEST(test_the_pins_are_gpio_2_4_5_18_19);
    RUN_TEST(test_every_pin_exists_on_both_machines_and_is_free_on_both);
    RUN_TEST(test_no_pin_offers_an_adc);
    RUN_TEST(test_every_pwm_row_drives_the_pin_that_names_it);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_the_trailer_bound_is_16_mib_less_the_trailer);
    return UNITY_END();
}
