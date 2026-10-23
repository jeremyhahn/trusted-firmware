/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 port's arithmetic, checked on x86 against the file the M7
 * image links (src/hal/cortex-m/imx95/soc_calc.c).
 *
 * The divisors are literals worked by hand from the LPUART's formula,
 * baud = clk / (OSR+1) / SBR, at the 24 MHz root the board profile names:
 * 115200 is 208.33 clocks a bit, which 4x52, 8x26, 13x16, 16x13 and 26x8
 * come equally near, so the highest ratio, 26, wins; 9600 is 2500 exactly,
 * 25x100 the highest of the exact ones. A computed vector would move with the
 * code it pins.
 */

#include "unity.h"
#include "soc_calc.h"

void setUp(void) {}
void tearDown(void) {}

#define CLK 24000000u

static void test_115200_takes_the_highest_ratio_of_equal_error(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpuart_baud(CLK, 115200u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x19000008u, reg);    /* OSR 25 (x26), SBR 8 */
}

static void test_9600_is_exact(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpuart_baud(CLK, 9600u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x18000064u, reg);    /* OSR 24 (x25), SBR 100 */
}

static void test_a_ratio_below_8_sets_bothedge(void)
{
    uint32_t reg = 0u;

    /* 4 Mbaud is 6 clocks a bit: x6, SBR 1, the only exact divisor. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpuart_baud(CLK, 4000000u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x05020001u, reg);
}

static void test_a_zero_baud_or_clock_is_out_of_range(void)
{
    uint32_t reg = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpuart_baud(CLK, 0u, &reg));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpuart_baud(0u, 115200u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, reg);    /* untouched on a refusal */
}

static void test_a_baud_past_the_clock_is_out_of_range(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpuart_baud(CLK, 12000000u, &reg));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpuart_baud(CLK, 0xFFFFFFFFu, &reg));
}

static void test_a_divisor_more_than_3_percent_off_is_refused(void)
{
    uint32_t reg = 0u;

    /* 5 Mbaud is 4.8 clocks a bit: x5 gives 4.8 Mbaud, 4 % slow, the nearest. */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpuart_baud(CLK, 5000000u, &reg));
}

static void test_a_baud_below_the_divisor_is_out_of_range(void)
{
    uint32_t reg = 0u;

    /* 50 baud wants SBR 15000 at x32, past the field's 8191. */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpuart_baud(CLK, 50u, &reg));
    /* 300 baud is SBR 2500 at x32. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpuart_baud(CLK, 300u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x1F0009C4u, reg);
}

static void test_the_gpio_banks(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x47400000u, tfw_imx95_gpio_base(0u));
    TEST_ASSERT_EQUAL_HEX32(0x43810000u, tfw_imx95_gpio_base(1u));
    TEST_ASSERT_EQUAL_HEX32(0x43820000u, tfw_imx95_gpio_base(2u));
    TEST_ASSERT_EQUAL_HEX32(0x43840000u, tfw_imx95_gpio_base(3u));
    TEST_ASSERT_EQUAL_HEX32(0x43850000u, tfw_imx95_gpio_base(4u));
}

static void test_a_bank_past_gpio5_has_no_base(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_imx95_gpio_base(5u));
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_imx95_gpio_base(0xFFu));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_115200_takes_the_highest_ratio_of_equal_error);
    RUN_TEST(test_9600_is_exact);
    RUN_TEST(test_a_ratio_below_8_sets_bothedge);
    RUN_TEST(test_a_zero_baud_or_clock_is_out_of_range);
    RUN_TEST(test_a_baud_past_the_clock_is_out_of_range);
    RUN_TEST(test_a_divisor_more_than_3_percent_off_is_refused);
    RUN_TEST(test_a_baud_below_the_divisor_is_out_of_range);
    RUN_TEST(test_the_gpio_banks);
    RUN_TEST(test_a_bank_past_gpio5_has_no_base);
    return UNITY_END();
}
