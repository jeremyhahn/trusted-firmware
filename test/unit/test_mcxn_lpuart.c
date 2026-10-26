/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x LPUART's BAUD register arithmetic, checked on x86
 * (src/hal/cortex-m/mcxn94x/lpuart_baud.c). The register write itself is
 * hal_uart.c's and runs only on the part.
 *
 * baud = clk / (ratio * SBR), ratio 4..32. The expected values are worked by
 * hand from the divisors of clk / baud, and are literals on purpose: a value
 * computed by the code under test would move with it.
 */

#include "unity.h"
#include "lpuart_baud.h"

#define FRO12M 12000000uL

void setUp(void) {}
void tearDown(void) {}

/* 12e6 / 115200 = 104.17: ratio 26 x SBR 4 = 104, 115385 baud, 0.16% fast.
 * Ratios 4, 8 and 13 reach 104 too; the largest wins the tie. */
static void test_115200_from_fro12m_is_ratio_26_sbr_4(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_lpuart_baud(FRO12M, 115200u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x19000004u, reg);
}

/* 12e6 / 9600 = 1250 = 25 x 50, exact. */
static void test_9600_from_fro12m_is_ratio_25_sbr_50(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_lpuart_baud(FRO12M, 9600u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x18000032u, reg);
}

/* 12e6 / 1e6 = 12: ratio 12 x SBR 1 beats 6 x 2 and 4 x 3 on the tie. */
static void test_a_tie_takes_the_largest_ratio(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_lpuart_baud(FRO12M, 1000000u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x0B000001u, reg);
}

/* 12e6 / 2e6 = 6: only ratio 6 x SBR 1 is exact, and a ratio below 8 samples
 * on both edges. */
static void test_a_ratio_below_8_sets_bothedge(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_lpuart_baud(FRO12M, 2000000u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x05020001u, reg);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_LPUART_BAUD_BOTHEDGE, reg & TFW_MCXN_LPUART_BAUD_BOTHEDGE);
}

/* 12e6 / 3e6 = 4, the smallest ratio with SBR 1: the fastest rate. */
static void test_the_fastest_rate_is_a_quarter_of_the_clock(void)
{
    uint32_t reg = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_lpuart_baud(FRO12M, 3000000u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0x03020001u, reg);
}

static void test_a_zero_rate_or_clock_is_refused(void)
{
    uint32_t reg = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpuart_baud(FRO12M, 0u, &reg));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpuart_baud(0u, 115200u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, reg);
}

/* 4 Mbaud: the nearest is 3 Mbaud at ratio 4 x SBR 1, 25% slow. */
static void test_a_rate_above_the_reach_is_refused(void)
{
    uint32_t reg = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpuart_baud(FRO12M, 4000000u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, reg);
}

/* 12e6 / (32 x 8191) = 45.8 baud is the slowest; 30 baud is 53% off it. */
static void test_a_rate_below_the_reach_is_refused(void)
{
    uint32_t reg = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpuart_baud(FRO12M, 30u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, reg);
}

/* 12e6 / 2.5e6 = 4.8: ratio 5 x SBR 1 gives 2.4 Mbaud, 4% slow, past the 3%
 * the SDK allows; ratio 4 gives 3 Mbaud, 20% fast. */
static void test_a_rate_more_than_3_percent_off_is_refused(void)
{
    uint32_t reg = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_lpuart_baud(FRO12M, 2500000u, &reg));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, reg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_115200_from_fro12m_is_ratio_26_sbr_4);
    RUN_TEST(test_9600_from_fro12m_is_ratio_25_sbr_50);
    RUN_TEST(test_a_tie_takes_the_largest_ratio);
    RUN_TEST(test_a_ratio_below_8_sets_bothedge);
    RUN_TEST(test_the_fastest_rate_is_a_quarter_of_the_clock);
    RUN_TEST(test_a_zero_rate_or_clock_is_refused);
    RUN_TEST(test_a_rate_above_the_reach_is_refused);
    RUN_TEST(test_a_rate_below_the_reach_is_refused);
    RUN_TEST(test_a_rate_more_than_3_percent_off_is_refused);
    return UNITY_END();
}
