/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32-P4 PoE board's pin table, checked on x86.
 *
 * On this port a pin is a GPIO number: the port row holds the bank's first
 * GPIO and the coordinate's bit is the offset into it (src/hal/esp32/
 * hal_types.h). On the host the port row's value is the mock's key, so the
 * GPIO a pin drives is key + bit here exactly as it is base + bit there.
 *
 * What is worth more than the rest: NO PIN THE BOARD SPENDS ELSEWHERE IS
 * OFFERED. The RMII lines, the PHY's management pins, the C6's SDIO and its
 * enable, USB and the console are absent, because a host that drove the C6's
 * enable high would start the co-processor this firmware promises to hold in
 * reset, and one that drove an RMII line would take the wire down under
 * itself.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include <stdio.h>
#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t gpio_of_pin[TFW_PIN_COUNT] = {
    2, 3, 4, 5, 6, 7, 8, 20, 21, 22, 23, 32, 33, 46, 47, 48, 53
};

static uint8_t gpio(tfw_pin_t pin)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(pin, &r));
    return (uint8_t)(r.port + r.bit);
}

static void test_the_board_is_the_first_esp32_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x80u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("esp32p4-poe", tfw_board_name());
    TEST_ASSERT_EQUAL_UINT8(TFW_PIN_COUNT, tfw_board_pin_count());
}

static void test_every_pin_is_the_gpio_its_row_names(void)
{
    char msg[24];

    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        snprintf(msg, sizeof msg, "pin %u", (unsigned)i);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(gpio_of_pin[i], gpio(i), msg);
    }
}

static void test_the_second_bank_starts_at_gpio_32(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(11u, &r));
    TEST_ASSERT_EQUAL_UINT16(32u, r.port);
    TEST_ASSERT_EQUAL_UINT8(0u, r.bit);
}

static void test_no_pin_the_board_spends_elsewhere_is_offered(void)
{
    static const uint8_t reserved[] = {
        14, 15, 16, 17, 18, 19,                 /* the C6's SDIO */
        24, 25, 26, 27,                         /* USB */
        28, 29, 30, 31, 34, 35, 49, 50, 51, 52, /* RMII, MDC/MDIO, PHY reset */
        36, 37, 38,                             /* strapping, console UART0 */
        TFW_ESP_COPROC_EN_GPIO,
        TFW_ESP_ETH_MDC_GPIO, TFW_ESP_ETH_MDIO_GPIO, TFW_ESP_ETH_PHY_RST_GPIO,
        TFW_ESP_ETH_RMII_CLK_GPIO, TFW_ESP_ETH_RMII_TX_EN_GPIO,
        TFW_ESP_ETH_RMII_TXD0_GPIO, TFW_ESP_ETH_RMII_TXD1_GPIO,
        TFW_ESP_ETH_RMII_CRS_DV_GPIO, TFW_ESP_ETH_RMII_RXD0_GPIO,
        TFW_ESP_ETH_RMII_RXD1_GPIO,
    };

    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        for (size_t j = 0; j < sizeof reserved; j++) {
            TEST_ASSERT_NOT_EQUAL_UINT8(reserved[j], gpio(i));
        }
    }
}

static void test_the_coprocessor_enable_is_gpio_54(void)
{
    TEST_ASSERT_EQUAL(54, TFW_ESP_COPROC_EN_GPIO);
}

static void test_no_two_pins_share_a_gpio(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        for (uint8_t j = (uint8_t)(i + 1u); j < TFW_PIN_COUNT; j++) {
            TEST_ASSERT_NOT_EQUAL_UINT8(gpio(i), gpio(j));
        }
    }
}

static void test_adc1_channel_n_is_gpio_16_plus_n(void)
{
    uint8_t adc_pins = 0u;

    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch;

        if (tfw_board_adc_channel(i, &ch) == TFW_OK) {
            TEST_ASSERT_EQUAL_UINT8(gpio(i) - 16u, ch);
            TEST_ASSERT_LESS_THAN_UINT8(TFW_ADC_COUNT, ch);
            TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
            adc_pins++;
        }
    }
    TEST_ASSERT_EQUAL_UINT8(4u, adc_pins);
}

static void test_every_pwm_row_drives_the_pin_that_names_it(void)
{
    uint8_t pwm_pins = 0u;

    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;

        if (tfw_board_pwm(i, &p) == TFW_OK) {
            /* host keys: ctl is the LEDC channel, duty the GPIO */
            TEST_ASSERT_EQUAL_UINT16(gpio(i), p.duty);
            TEST_ASSERT_EQUAL_UINT16(pwm_pins, p.ctl);
            TEST_ASSERT_EQUAL_UINT8(1u, tfw_board_pin_pwm_state(i));
            pwm_pins++;
        }
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_PWM_COUNT, pwm_pins);
}

static void test_the_page_carries_bank_and_offset(void)
{
    uint8_t page[TFW_PIN_PAGE_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin_page(16u, page));
    TEST_ASSERT_EQUAL_HEX8(1u, page[2]);
    TEST_ASSERT_EQUAL_HEX8(21u, page[3]);
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
}

static void test_attest_stays_off_until_ds_has_a_wire_encoding(void)
{
    TEST_ASSERT_EQUAL(0, TFW_FEATURE_ATTEST);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_the_first_esp32_id);
    RUN_TEST(test_every_pin_is_the_gpio_its_row_names);
    RUN_TEST(test_the_second_bank_starts_at_gpio_32);
    RUN_TEST(test_no_pin_the_board_spends_elsewhere_is_offered);
    RUN_TEST(test_the_coprocessor_enable_is_gpio_54);
    RUN_TEST(test_no_two_pins_share_a_gpio);
    RUN_TEST(test_adc1_channel_n_is_gpio_16_plus_n);
    RUN_TEST(test_every_pwm_row_drives_the_pin_that_names_it);
    RUN_TEST(test_the_page_carries_bank_and_offset);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_attest_stays_off_until_ds_has_a_wire_encoding);
    return UNITY_END();
}
