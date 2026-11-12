/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7 tone's GPT arithmetic (src/hal/cortex-m/imx95/gpt.h, #22):
 * the prescaler and compare value a tone frequency sets, at the 24 MHz clock
 * the header assumes (TO CONFIRM there).
 */
#include <stdint.h>

#include "unity.h"
#include "gpt.h"

void setUp(void) {}
void tearDown(void) {}

/* 440 Hz: 24 MHz / 880 = 27272 counts a half period, OCR1 one less. */
void test_gpt_tone_440_compare(void)
{
    uint32_t pr, ocr;

    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 440u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(27271u, ocr);
}

/* src/caps/tone.c's 31 Hz floor: 387096 counts, no prescaler needed. */
void test_gpt_tone_31_hz_floor(void)
{
    uint32_t pr, ocr;

    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 31u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(387095u, ocr);
}

/* 20 kHz, the top of the audible range: 600 counts. */
void test_gpt_tone_20_khz(void)
{
    uint32_t pr, ocr;

    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 20000u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(599u, ocr);
}

/* Zero hertz, or a tone past half the clock, has no compare value. */
void test_gpt_tone_zero_and_past_the_clock(void)
{
    uint32_t pr = 7u, ocr = 7u;

    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, 0u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(0u, ocr);
    pr = ocr = 7u;
    tfw_imx95_gpt_half(10u, 6u, &pr, &ocr);
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL_UINT32(0u, ocr);
}

/* The bits hal_tone.c composes, at Linux's positions. */
void test_gpt_control_bits(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000080u, TFW_IMX95_GPT_CR_CLKSRC(TFW_IMX95_GPT_CR_CLKSRC_PER));
    TEST_ASSERT_EQUAL_HEX32(0x00100000u, TFW_IMX95_GPT_CR_OM1(TFW_IMX95_GPT_CR_OM1_TOGGLE));
    TEST_ASSERT_EQUAL_HEX32(0x00000200u, TFW_IMX95_GPT_CR_FRR);
    TEST_ASSERT_EQUAL_HEX32(0x00008000u, TFW_IMX95_GPT_CR_SWR);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gpt_tone_440_compare);
    RUN_TEST(test_gpt_tone_31_hz_floor);
    RUN_TEST(test_gpt_tone_20_khz);
    RUN_TEST(test_gpt_tone_zero_and_past_the_clock);
    RUN_TEST(test_gpt_control_bits);
    return UNITY_END();
}
