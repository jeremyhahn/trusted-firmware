/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 AN500 board's pin table, checked on x86.
 *
 * QEMU models two user LEDs on this machine and nothing else this port
 * drives as a pin, so the table is the two LEDs on port 0, bits 0 and 1, then
 * the shield 0 SPI select, MISC bit 8 on port 1 (AN500, Arm DAI 0500B, 3.7
 * and 3.13 Table 3-6), and no pin offers a converter or a compare unit.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"
#include "tfw/config.h"
#include "tfw/protocol.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_board_is_the_cortex_m7_qemu_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x42u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("mps2-an500", tfw_board_name());
}

static void test_the_pins_are_the_two_leds_and_the_spi_select(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL_UINT8(3u, TFW_PIN_COUNT);
    TEST_ASSERT_EQUAL_UINT8(2u, TFW_PORT_COUNT);
    for (uint8_t i = 0u; i < 2u; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(0u, r.port);
        TEST_ASSERT_EQUAL_UINT8(i, r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_LED);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
    }
}

/* The select is a digital output, so it is flagged digital; hal_gpio.c still
 * refuses port 1 to the GPIO commands. Nothing claims it, so SPI_XFER, which
 * refuses a reserved or in-use select, accepts it. */
static void test_pin_2_is_the_shield_0_spi_select(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(2u, &r));
    TEST_ASSERT_EQUAL_UINT8(1u, r.port);
    TEST_ASSERT_EQUAL_UINT8(8u, r.bit);
    TEST_ASSERT_EQUAL_HEX8(0x01u, tfw_board_pin_flags(2u));
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

/* An OTA_DATA or OTA_AUTH request is 70 bytes on the wire, and QEMU's UART
 * delivers it with no gap between bytes. The 64-byte ring held 63 of them and
 * dropped the rest, and the A/B wire row hung on 0x69 and 0x6f. */
static void test_the_receive_ring_holds_a_whole_longest_request(void)
{
    TEST_ASSERT_EQUAL_UINT(128u, TFW_UART_RX_RING);
    TEST_ASSERT_EQUAL_UINT(64u, TFW_UART_TX_RING);
    TEST_ASSERT_EQUAL_UINT(70u, TFW_MAX_PAYLOAD + TFW_LEN_MIN + TFW_FRAME_OVERHEAD);
    TEST_ASSERT_EQUAL_UINT(71u, TFW_MAX_FRAME);
    TEST_ASSERT_TRUE(TFW_UART_RX_RING - 1u >= 71u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_the_cortex_m7_qemu_id);
    RUN_TEST(test_the_pins_are_the_two_leds_and_the_spi_select);
    RUN_TEST(test_pin_2_is_the_shield_0_spi_select);
    RUN_TEST(test_no_pin_offers_an_adc_or_a_pwm);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_the_receive_ring_holds_a_whole_longest_request);
    return UNITY_END();
}
