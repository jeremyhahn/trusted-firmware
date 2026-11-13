/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7 tone's GPT arithmetic (src/hal/cortex-m/imx95/gpt.h, #22):
 * the prescaler and compare value a tone frequency sets, at the 24 MHz clock
 * the header assumes (TO CONFIRM there).
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "gpt.h"
#include "imx95_model.h"

/* hal_tone.c against the board's register model for its pin and NVIC, and
 * against the GPT registers below, which mk/host.mk sends TFW_IMX95_GPT_REG
 * to: the model has no GPT (the base is TO CONFIRM). */
static uint32_t gpt[TFW_IMX95_GPT_CNT / 4u + 1u];
static const tfw_pinref_t pin = { .port = 1u, .bit = 1u };
/* The model settles a write at its next access: read it afresh each time. */
#define M (tfw_imx95_model_get())

void tfw_cm_irq0(void);

volatile uint32_t *tfw_test_gpt_reg(uint32_t off)
{
    return (volatile uint32_t *)&gpt[off / 4u];
}

void setUp(void)
{
    tfw_imx95_model_reset();
    for (unsigned i = 0u; i < sizeof gpt / sizeof gpt[0]; i++) {
        gpt[i] = 0u;
    }
}
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

/* A start sets the pin an output, the compare value, its interrupt and the
 * timer on in restart mode, and enables the vector. */
void test_tone_start_programs_the_gpt(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    TEST_ASSERT_EQUAL_HEX32(0x2u, M->gpio[1].pddr & 0x2u);
    TEST_ASSERT_EQUAL_UINT32(0u, gpt[TFW_IMX95_GPT_PR / 4u]);
    TEST_ASSERT_EQUAL_UINT32(27271u, gpt[TFW_IMX95_GPT_OCR1 / 4u]);
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_GPT_IR_OF1IE, gpt[TFW_IMX95_GPT_IR / 4u]);
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_GPT_CR_EN | TFW_IMX95_GPT_CR_CLKSRC(TFW_IMX95_GPT_CR_CLKSRC_PER),
                            gpt[TFW_IMX95_GPT_CR / 4u]);
    TEST_ASSERT_EQUAL_HEX32(0u, gpt[TFW_IMX95_GPT_CR / 4u] & TFW_IMX95_GPT_CR_FRR);
    TEST_ASSERT_EQUAL_HEX32(1u << TFW_IMX95_GPT_IRQ, M->nvic.iser[0] & (1u << TFW_IMX95_GPT_IRQ));
    TEST_ASSERT_EQUAL_UINT32(0u, M->unmodelled);
    (void)tfw_hal_tone_stop();
}

/* Each match clears the flag and toggles the pin. */
void test_tone_match_toggles_the_pin(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 31u));
    tfw_cm_irq0();
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_GPT_SR_OF1, gpt[TFW_IMX95_GPT_SR / 4u]);
    TEST_ASSERT_EQUAL_HEX32(0x2u, M->gpio[1].pdor & 0x2u);
    tfw_cm_irq0();
    TEST_ASSERT_EQUAL_HEX32(0u, M->gpio[1].pdor & 0x2u);
    (void)tfw_hal_tone_stop();
}

/* A second start retunes the one tone. */
void test_tone_restart_retunes(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 20000u));
    TEST_ASSERT_EQUAL_UINT32(599u, gpt[TFW_IMX95_GPT_OCR1 / 4u]);
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_GPT_CR_EN, gpt[TFW_IMX95_GPT_CR / 4u] & TFW_IMX95_GPT_CR_EN);
    (void)tfw_hal_tone_stop();
}

/* A stop, or zero hertz, halts the timer and drives the pin low; a match
 * after it leaves the pin alone. */
void test_tone_stop_drives_the_pin_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    tfw_cm_irq0();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_HEX32(0u, gpt[TFW_IMX95_GPT_CR / 4u]);
    TEST_ASSERT_EQUAL_HEX32(0u, gpt[TFW_IMX95_GPT_IR / 4u]);
    TEST_ASSERT_EQUAL_HEX32(0u, M->gpio[1].pdor & 0x2u);
    tfw_cm_irq0();
    TEST_ASSERT_EQUAL_HEX32(0u, M->gpio[1].pdor & 0x2u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 440u));
    tfw_cm_irq0();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin, 0u));
    TEST_ASSERT_EQUAL_HEX32(0u, gpt[TFW_IMX95_GPT_CR / 4u]);
    TEST_ASSERT_EQUAL_HEX32(0u, M->gpio[1].pdor & 0x2u);
}

/* A pin past GPIO5 is refused before the timer is touched. */
void test_tone_bad_pin_is_refused(void)
{
    const tfw_pinref_t bank6 = { .port = 5u, .bit = 0u };

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&bank6, 440u));
    TEST_ASSERT_EQUAL_HEX32(0u, gpt[TFW_IMX95_GPT_CR / 4u]);
}

/* test_imx95_gpt_nobase: the board's base, zero until question 17 is answered.
 * Every call answers TFW_ERR_UNSUPPORTED and touches no register: no GPT
 * word, no pin direction, no NVIC enable. */
void test_tone_without_a_base_is_unsupported(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_tone_start(&pin, 440u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_tone_start(&pin, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_tone_stop());
    for (unsigned i = 0u; i < sizeof gpt / sizeof gpt[0]; i++) {
        TEST_ASSERT_EQUAL_HEX32(0u, gpt[i]);
    }
    TEST_ASSERT_EQUAL_HEX32(0u, M->gpio[1].pddr);
    TEST_ASSERT_EQUAL_HEX32(0u, M->nvic.iser[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, M->unmodelled);
}

int main(void)
{
    UNITY_BEGIN();
    if (TFW_IMX95_GPT_BASE == 0u) {
        RUN_TEST(test_tone_without_a_base_is_unsupported);
    } else {
        RUN_TEST(test_tone_start_programs_the_gpt);
        RUN_TEST(test_tone_match_toggles_the_pin);
        RUN_TEST(test_tone_restart_retunes);
        RUN_TEST(test_tone_stop_drives_the_pin_low);
        RUN_TEST(test_tone_bad_pin_is_refused);
    }
    RUN_TEST(test_gpt_tone_440_compare);
    RUN_TEST(test_gpt_tone_31_hz_floor);
    RUN_TEST(test_gpt_tone_20_khz);
    RUN_TEST(test_gpt_tone_zero_and_past_the_clock);
    RUN_TEST(test_gpt_control_bits);
    return UNITY_END();
}
