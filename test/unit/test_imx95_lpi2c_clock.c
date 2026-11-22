/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7's LPI2C clock arithmetic, tfw_imx95_lpi2c_clock() in
 * src/hal/cortex-m/imx95/lpi2c.h, unchanged, against Linux v6.12's
 * lpi2c_imx_config() (drivers/i2c/busses/i2c-imx-lpi2c.c) restated here in
 * Linux's own form: ref() below, written from the driver and not from the
 * header, so an off-by-one in either shows as a mismatch.
 *
 * test_imx95_lpi2c pins 100 kHz from 24 and 16 MHz; this file pins fast mode,
 * fast-mode plus, the prescaler step at the 64-count low limit, the smallest
 * cycle the header accepts, and a sweep of roots and bus rates.
 */
#include <stdio.h>

#include "unity.h"
#include "../../src/hal/cortex-m/imx95/lpi2c.h"

#define SENTINEL 0xEEu

struct ref_out {
    int      ok;
    uint32_t prescale, clklo, clkhi;
};

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1u) / (d))

/* lpi2c_imx_config(): the smallest prescale whose low count fits six bits.
 * Refuses a zero bus, a quotient of 4 or under (the header's floor, where the
 * cycle would be 1 or less), and a rate no prescale reaches. */
static struct ref_out ref(uint32_t clk, uint32_t bus)
{
    struct ref_out r = { 0, 0u, 0u, 0u };
    uint64_t       prescale;

    if (bus == 0u) {
        return r;
    }
    for (prescale = 0u; prescale <= 7u; prescale++) {
        uint64_t div = (uint64_t)clk / ((1ull << prescale) * bus);
        uint64_t clk_cycle, clkhi, clklo;

        if (div <= 4u) {
            return r;
        }
        clk_cycle = div - 3u;
        clkhi     = DIV_ROUND_UP(clk_cycle, 3u);
        clklo     = clk_cycle - clkhi;
        if (clklo < 64u) {
            r.ok       = 1;
            r.prescale = (uint32_t)prescale;
            r.clklo    = (uint32_t)clklo;
            r.clkhi    = (uint32_t)clkhi;
            return r;
        }
    }
    return r;
}

void setUp(void)
{
}

void tearDown(void)
{
}

/* 400 kHz from 24 MHz: prescaler 0, a 57-count cycle split 38 low, 19 high. */
static void test_lpi2c_clock_fast_mode_at_24mhz(void)
{
    uint32_t c1 = SENTINEL, c0 = SENTINEL;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_clock(24000000uL, 400000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(0u, c1);
    TEST_ASSERT_EQUAL_HEX32(0x09131326u, c0);   /* DATAVD 9, SETHOLD 19, HI 19, LO 38 */
}

/* 1 MHz from 24 MHz: a 21-count cycle split 14 and 7. */
static void test_lpi2c_clock_fast_plus_at_24mhz(void)
{
    uint32_t c1 = SENTINEL, c0 = SENTINEL;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_clock(24000000uL, 1000000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(0u, c1);
    TEST_ASSERT_EQUAL_HEX32(0x0307070Eu, c0);   /* DATAVD 3, SETHOLD 7, HI 7, LO 14 */
}

/* A 95-count cycle: HI 32, LO 63, the largest low count prescaler 0 keeps. */
static void test_lpi2c_clock_low_count_63_keeps_prescaler_0(void)
{
    uint32_t c1 = SENTINEL, c0 = SENTINEL;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_clock(98000000uL, 1000000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(0u, c1);
    TEST_ASSERT_EQUAL_HEX32(0x1020203Fu, c0);   /* DATAVD 16, SETHOLD 32, HI 32, LO 63 */
}

/* A 96-count cycle would leave LO 64: prescaler 1, a 46-count cycle, 30 and 16. */
static void test_lpi2c_clock_low_count_64_takes_prescaler_1(void)
{
    uint32_t c1 = SENTINEL, c0 = SENTINEL;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_clock(99000000uL, 1000000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(1u, c1);
    TEST_ASSERT_EQUAL_HEX32(0x0810101Eu, c0);   /* DATAVD 8, SETHOLD 16, HI 16, LO 30 */
}

/* A quotient of 5 is the smallest accepted (a 2-count cycle, 1 and 1); 4 is refused. */
static void test_lpi2c_clock_smallest_cycle(void)
{
    uint32_t c1 = SENTINEL, c0 = SENTINEL;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_lpi2c_clock(5000000uL, 1000000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(0u, c1);
    TEST_ASSERT_EQUAL_HEX32(0x00010101u, c0);   /* DATAVD 0, SETHOLD 1, HI 1, LO 1 */

    c1 = SENTINEL;
    c0 = SENTINEL;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_lpi2c_clock(4000000uL, 1000000uL, &c1, &c0));
    TEST_ASSERT_EQUAL_UINT32(SENTINEL, c1);
    TEST_ASSERT_EQUAL_UINT32(SENTINEL, c0);
}

/* Every root against every standard bus rate gives Linux's prescale and split,
 * the counts fit six bits, and one prescale less would not fit. */
static void test_lpi2c_clock_sweep_matches_linux(void)
{
    static const uint32_t clks[] = { 16000000uL, 24000000uL, 48000000uL, 66500000uL,
                                     133000000uL, 200000000uL, 400000000uL };
    static const uint32_t buses[] = { 100000uL, 400000uL, 1000000uL, 3400000uL };
    unsigned i, j;

    for (i = 0u; i < sizeof clks / sizeof clks[0]; i++) {
        for (j = 0u; j < sizeof buses / sizeof buses[0]; j++) {
            struct ref_out r  = ref(clks[i], buses[j]);
            uint32_t       c1 = SENTINEL, c0 = SENTINEL;
            tfw_status_t   st = tfw_imx95_lpi2c_clock(clks[i], buses[j], &c1, &c0);
            char           msg[64];

            snprintf(msg, sizeof msg, "clk %lu Hz, bus %lu Hz",
                     (unsigned long)clks[i], (unsigned long)buses[j]);
            if (!r.ok) {
                TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_RANGE, st, msg);
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(SENTINEL, c1, msg);
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(SENTINEL, c0, msg);
                continue;
            }
            TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, st, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(r.prescale, c1, msg);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(
                TFW_LPI2C_MCCR0_VAL(r.clklo, r.clkhi, r.clkhi, r.clkhi >> 1), c0, msg);
            TEST_ASSERT_TRUE_MESSAGE(r.clklo < 64u, msg);
            TEST_ASSERT_TRUE_MESSAGE(r.clkhi < 64u, msg);
            if (r.prescale != 0u) {
                uint64_t below = (uint64_t)clks[i] / ((1ull << (r.prescale - 1u)) * buses[j]) - 3u;

                TEST_ASSERT_TRUE_MESSAGE(below - DIV_ROUND_UP(below, 3u) >= 64u, msg);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lpi2c_clock_fast_mode_at_24mhz);
    RUN_TEST(test_lpi2c_clock_fast_plus_at_24mhz);
    RUN_TEST(test_lpi2c_clock_low_count_63_keeps_prescaler_0);
    RUN_TEST(test_lpi2c_clock_low_count_64_takes_prescaler_1);
    RUN_TEST(test_lpi2c_clock_smallest_cycle);
    RUN_TEST(test_lpi2c_clock_sweep_matches_linux);
    return UNITY_END();
}
