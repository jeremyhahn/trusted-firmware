/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw_imx95_gpt_half() (src/hal/cortex-m/imx95/gpt.h) at its edges and over
 * every tone the HAL can ask for. The reference is the comment above it:
 * half = clk/(2*hz) counts, OCR1 = half/(PR+1) - 1 with PR the smallest
 * divider that fits 32 bits, and hz zero or a half period under one count
 * gives PR 0 and OCR1 0.
 */
#include <stdint.h>
#include <stdio.h>

#include "unity.h"
#include "../../src/hal/cortex-m/imx95/gpt.h"

void setUp(void) {}
void tearDown(void) {}

static void test_gpt_half_uint16_ceiling(void)
{
    uint32_t pr = 7u, ocr = 7u;

    /* 24000000 / 131070 = 183.1 counts. */
    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 65535u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(182u, ocr);
}

static void test_gpt_half_rounds_a_fractional_half_down(void)
{
    uint32_t pr = 7u, ocr = 7u;

    /* 24000000 / 74 = 324324.3 counts. */
    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 37u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(324323u, ocr);
}

static void test_gpt_half_one_count_edge(void)
{
    uint32_t pr = 7u, ocr = 7u;

    /* A half of one count and a half of none give the same pair, PR 0 and
     * OCR1 0; hal_tone.c passes a uint16_t hz, so it reaches neither. */
    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 12000000u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(0u, ocr);

    pr = ocr = 7u;
    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 12000001u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(0u, ocr);
}

static void test_gpt_half_widest_clock_needs_no_prescaler(void)
{
    uint32_t pr = 7u, ocr = 7u;

    /* half is at most 0xFFFFFFFF / 2, so the prescaler loop never runs. */
    tfw_imx95_gpt_half(0xFFFFFFFFu, 1u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(2147483646u, ocr);
}

static void test_gpt_half_one_hertz(void)
{
    uint32_t pr = 7u, ocr = 7u;

    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 1u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(11999999u, ocr);
}

static void test_gpt_half_sweep_every_uint16_tone(void)
{
    char msg[32];
    uint32_t hz;

    for (hz = 1u; hz <= 65535u; hz++) {
        uint32_t pr = 7u, ocr = 7u;
        uint64_t want = 24000000uLL / (2uLL * (uint64_t)hz);

        tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, hz, &pr, &ocr);
        if (pr != 0u || (uint64_t)ocr + 1u != want) {
            snprintf(msg, sizeof msg, "hz %lu", (unsigned long)hz);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, pr, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)want, ocr + 1u, msg);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gpt_half_uint16_ceiling);
    RUN_TEST(test_gpt_half_rounds_a_fractional_half_down);
    RUN_TEST(test_gpt_half_one_count_edge);
    RUN_TEST(test_gpt_half_widest_clock_needs_no_prescaler);
    RUN_TEST(test_gpt_half_one_hertz);
    RUN_TEST(test_gpt_half_sweep_every_uint16_tone);
    return UNITY_END();
}
