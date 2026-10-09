/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A pin coordinate wider than a nibble, from the board table to the wire and to
 * the HAL (#12).
 *
 * The pin page carries a whole byte of port and a whole byte of bit. The first
 * cut of it still unpacked the table as nibbles in core, so no firmware could
 * ever send port 18 or bit 31 -- the page advertised a width nothing could
 * fill, and tfw_board_pin() handed the HAL the same truncated bit. The packing
 * is now the architecture's (hal_types.h); the host's coordinate is two whole
 * bytes, so this suite can name a pin the AVR cannot and prove core passes it
 * through untouched.
 *
 * It supplies its own three board tables in place of boards/<board>/board.c:
 * no real board has a port 18, and none should be invented for a test. The
 * pin count is the host board profile's; the rows this suite does not name are
 * zero and unused.
 */

#include "unity.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "board_profile.h"

#define WIDE_PORT  18u
#define WIDE_BIT   31u
#define PORT_KEY(n) ((uint16_t)(0x4000u + (n)))

const tfw_port_desc_t tfw_board_ports[WIDE_PORT + 1u] = {
    [2]         = { PORT_KEY(2u) },          /* where 18's low nibble would land */
    [WIDE_PORT] = { PORT_KEY(WIDE_PORT) },
};

const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] = {
    [0] = { TFW_PB(WIDE_PORT, WIDE_BIT), 7u, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
    [1] = { TFW_PB(TFW_PORT_NONE, 0u),   6u, TFW_PWM_NONE, TFW_PIN_ADC },
};

const tfw_pwm_desc_t tfw_board_pwms[1] = { { 0u, 0u, 0u, 0u } };

void setUp(void)    { tfw_mock_reset(); }
void tearDown(void) {}

static void test_the_page_carries_port_18_and_bit_31_whole(void)
{
    uint8_t page[TFW_PIN_PAGE_LEN];
    const uint8_t want[TFW_PIN_PAGE_LEN] = {
        0u, TFW_PIN_DIGITAL | TFW_PIN_ADC, WIDE_PORT, WIDE_BIT, 7u, 0u,
    };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin_page(0u, page));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, page, TFW_PIN_PAGE_LEN);
}

/* The bit reaches the HAL as 31, on port 18's key -- not as 15, and not on the
 * port 2 that 18 becomes when it is squeezed into four bits. */
static void test_port_18_bit_31_reaches_the_hal_unchanged(void)
{
    tfw_pinref_t ref;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(0u, &ref));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&ref, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&ref, 1u));

    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_is_output(PORT_KEY(WIDE_PORT), WIDE_BIT));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(PORT_KEY(WIDE_PORT), WIDE_BIT));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_is_output(PORT_KEY(WIDE_PORT), WIDE_BIT & 0x0Fu));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_is_output(PORT_KEY(WIDE_PORT & 0x0Fu), WIDE_BIT & 0x0Fu));
}

/* The host's "no port" is its own TFW_PORT_NONE, and still goes out as the
 * wire's, in both bytes. */
static void test_a_pin_with_no_port_is_none_on_the_wire_and_refused_to_the_hal(void)
{
    uint8_t      page[TFW_PIN_PAGE_LEN];
    tfw_pinref_t ref;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin_page(1u, page));
    TEST_ASSERT_EQUAL_HEX8(TFW_WIRE_PORT_NONE, page[2]);
    TEST_ASSERT_EQUAL_HEX8(TFW_WIRE_PORT_NONE, page[3]);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pin(1u, &ref));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_page_carries_port_18_and_bit_31_whole);
    RUN_TEST(test_port_18_bit_31_reaches_the_hal_unchanged);
    RUN_TEST(test_a_pin_with_no_port_is_none_on_the_wire_and_refused_to_the_hal);
    return UNITY_END();
}
