/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x tone (src/hal/cortex-m/mcxn94x/hal_tone.c, ctimer_tone.h)
 * against the sparse register model (test/model/cortex-m/mcxn94x/): the
 * divider, the clock and CTIMER2 write order, retargeting, the stop and the
 * pin mux a GPIO mode command takes back. hal_tone.c keeps its state across
 * calls, so the first-start case runs first and setUp stops the tone.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "ctimer_tone.h"
#include "mcxn_model.h"

#define FRO12M 12000000uL

#define A_CT2(off)      (TFW_MCXN_CT2_BASE + (off))
#define A_TCR           A_CT2(TFW_MCXN_CT_TCR_OFF)
#define A_PR            A_CT2(TFW_MCXN_CT_PR_OFF)
#define A_MCR           A_CT2(TFW_MCXN_CT_MCR_OFF)
#define A_MR1           A_CT2(TFW_MCXN_CT_MR_OFF(1u))
#define A_EMR           A_CT2(TFW_MCXN_CT_EMR_OFF)
#define A_AHBCLKSET1    (TFW_MCXN_SYSCON_BASE + 0x220u + 4u)
#define A_PRESETCLR1    (TFW_MCXN_SYSCON_BASE + 0x140u + 4u)
#define A_CT2CLKSEL     (TFW_MCXN_SYSCON_BASE + 0x26Cu + 4u * TFW_MCXN_TONE_CT)
#define A_CT2CLKDIV     (TFW_MCXN_SYSCON_BASE + 0x3D0u + 4u * TFW_MCXN_TONE_CT)
#define A_CTGLOBAL      (TFW_MCXN_SYSCON_BASE + 0xE40u)
#define A_PCR(n, b)     (TFW_MCXN_PORT_BASE(n) + 0x80u + 4u * (b))
#define A_PCOR(n)       (TFW_MCXN_GPIO_BASE(n) + 0x48u)
#define A_PDDR(n)       (TFW_MCXN_GPIO_BASE(n) + 0x54u)

#define TONE_MUX ((TFW_MCXN_TONE_PIN_FN << TFW_MCXN_PCR_MUX_SHIFT) | TFW_MCXN_PCR_IBE)
#define EMR_TOGGLE (TFW_MCXN_CT_EMR_EMC_TOGGLE << TFW_MCXN_CT_EMR_EMC1_SHIFT)

static const tfw_pinref_t pin5  = { 3u, 19u };  /* P3_19, the board's TONE pin */
static const tfw_pinref_t other = { 0u, 10u };

void setUp(void)
{
    (void)tfw_hal_tone_stop();
    tfw_mcxn_model_reset();
}

void tearDown(void) {}

static uint32_t mr_for(uint32_t clk, uint16_t hz)
{
    uint32_t mr = 0xDEADBEEFu;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_mcxn_tone_match(clk, hz, &mr));
    return mr;
}

static void test_divider_at_fro12m(void)
{
    /* MR + 1 = 12 MHz / (2 * hz), rounded to nearest */
    TEST_ASSERT_EQUAL_UINT32(193547u, mr_for(FRO12M, 31u));   /* 193548.4 */
    TEST_ASSERT_EQUAL_UINT32(13635u, mr_for(FRO12M, 440u));   /* 13636.4 */
    TEST_ASSERT_EQUAL_UINT32(5999u, mr_for(FRO12M, 1000u));   /* 6000 */
    /* The highest a uint16_t asks: 91.55 rounds up to 92 counts. */
    TEST_ASSERT_EQUAL_UINT32(91u, mr_for(FRO12M, 65535u));
}

static void test_divider_refuses_out_of_range(void)
{
    uint32_t mr = 0xDEADBEEFu;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_tone_match(FRO12M, 0u, &mr));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_tone_match(0u, 440u, &mr));
    /* a half period of one count (2 Hz clock, 1 Hz tone) */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_tone_match(2u, 1u, &mr));
    /* above the clock: 0.76 counts rounds to 1 */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_mcxn_tone_match(100000u, 65535u, &mr));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, mr);
    /* the lowest accepted: two counts */
    TEST_ASSERT_EQUAL_UINT32(1u, mr_for(4u, 1u));
}

static void test_first_start_clocks_then_programs_held_in_reset(void)
{
    size_t i, clk_last = 0, ct_first, cen;

    tfw_mcxn_model_watch(A_TCR);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 440u));

    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CTRL1_TIMER2, tfw_mcxn_model_get(A_AHBCLKSET1));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CTRL1_TIMER2, tfw_mcxn_model_get(A_PRESETCLR1));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CTCLKSEL_FRO12M, tfw_mcxn_model_get(A_CT2CLKSEL));
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_mcxn_model_get(A_CT2CLKDIV));
    TEST_ASSERT_BITS_HIGH(TFW_MCXN_CTGLOBAL_CT2, tfw_mcxn_model_get(A_CTGLOBAL));

    /* every clock register before the first CTIMER2 one */
    for (i = 0; i < tfw_mcxn_model_log_len(); i++) {
        uintptr_t a = tfw_mcxn_model_log_addr(i);
        if (a == A_AHBCLKSET1 || a == A_PRESETCLR1 || a == A_CT2CLKSEL ||
            a == A_CT2CLKDIV || a == A_CTGLOBAL) {
            clk_last = i;
        }
    }
    ct_first = tfw_mcxn_model_log_first(A_TCR);
    TEST_ASSERT_TRUE(clk_last < ct_first);
    TEST_ASSERT_TRUE(tfw_mcxn_model_log_first(A_AHBCLKSET1) <
                     tfw_mcxn_model_log_first(A_PRESETCLR1));

    /* PR, MR1, MCR and EMR written with TCR at CRST, then CEN last */
    TEST_ASSERT_TRUE(tfw_mcxn_model_log_first(A_PR) > ct_first);
    {
        const uintptr_t prog[] = { A_PR, A_MR1, A_MCR, A_EMR };
        for (i = 0; i < sizeof prog / sizeof prog[0]; i++) {
            size_t at = tfw_mcxn_model_log_first(prog[i]);
            TEST_ASSERT_TRUE(at < tfw_mcxn_model_log_len());
            TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CRST, tfw_mcxn_model_log_watched(at));
        }
    }
    cen = tfw_mcxn_model_log_len();
    for (i = ct_first + 1u; i < tfw_mcxn_model_log_len(); i++) {
        if (tfw_mcxn_model_log_addr(i) == A_TCR) {
            cen = i;
        }
    }
    TEST_ASSERT_TRUE(cen > tfw_mcxn_model_log_first(A_EMR));
    TEST_ASSERT_TRUE(cen > tfw_mcxn_model_log_first(A_PCR(3u, 19u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CEN, tfw_mcxn_model_get(A_TCR));

    TEST_ASSERT_EQUAL_HEX32(0u, tfw_mcxn_model_get(A_PR));
    TEST_ASSERT_EQUAL_HEX32(13635u, tfw_mcxn_model_get(A_MR1));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_MCR_MR1R, tfw_mcxn_model_get(A_MCR));
    TEST_ASSERT_EQUAL_HEX32(EMR_TOGGLE, tfw_mcxn_model_get(A_EMR));
    TEST_ASSERT_EQUAL_HEX32(TONE_MUX, tfw_mcxn_model_get(A_PCR(3u, 19u)));
}

static void test_a_later_start_leaves_the_clock_alone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 1000u));
    TEST_ASSERT_EQUAL(tfw_mcxn_model_log_len(), tfw_mcxn_model_log_first(A_CT2CLKSEL));
    TEST_ASSERT_EQUAL(tfw_mcxn_model_log_len(), tfw_mcxn_model_log_first(A_AHBCLKSET1));
    TEST_ASSERT_EQUAL_HEX32(5999u, tfw_mcxn_model_get(A_MR1));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CEN, tfw_mcxn_model_get(A_TCR));
}

static void test_hz_0_stops(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 0u));
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_mcxn_model_get(A_EMR));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CRST, tfw_mcxn_model_get(A_TCR));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_PCR_IBE, tfw_mcxn_model_get(A_PCR(3u, 19u)));
}

static void test_retarget_frequency_keeps_the_pin(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 31u));
    TEST_ASSERT_EQUAL_HEX32(193547u, tfw_mcxn_model_get(A_MR1));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CEN, tfw_mcxn_model_get(A_TCR));
    TEST_ASSERT_EQUAL_HEX32(TONE_MUX, tfw_mcxn_model_get(A_PCR(3u, 19u)));
    /* the pin was not released in between */
    TEST_ASSERT_EQUAL(tfw_mcxn_model_log_len(), tfw_mcxn_model_log_first(A_PCOR(3u)));
}

static void test_retarget_pin_releases_the_old_one(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&other, 440u));
    TEST_ASSERT_EQUAL_HEX32(TONE_MUX, tfw_mcxn_model_get(A_PCR(0u, 10u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_PCR_IBE, tfw_mcxn_model_get(A_PCR(3u, 19u)));
    TEST_ASSERT_EQUAL_HEX32(1u << 19, tfw_mcxn_model_get(A_PCOR(3u)));
    TEST_ASSERT_BITS_HIGH(1u << 19, tfw_mcxn_model_get(A_PDDR(3u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CEN, tfw_mcxn_model_get(A_TCR));
}

static void test_stop_leaves_the_output_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 440u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_mcxn_model_get(A_EMR));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CRST, tfw_mcxn_model_get(A_TCR));
    /* GPIO mux, driven as an output, cleared */
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_PCR_IBE, tfw_mcxn_model_get(A_PCR(3u, 19u)));
    TEST_ASSERT_EQUAL_HEX32(1u << 19, tfw_mcxn_model_get(A_PCOR(3u)));
    TEST_ASSERT_BITS_HIGH(1u << 19, tfw_mcxn_model_get(A_PDDR(3u)));
    /* a second stop leaves the pin alone */
    tfw_mcxn_model_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL(tfw_mcxn_model_log_len(), tfw_mcxn_model_log_first(A_PCR(3u, 19u)));
}

static void test_a_bad_pin_is_refused_with_no_register_written(void)
{
    const tfw_pinref_t bad_port = { TFW_MCXN_PORT_COUNT, 0u };
    const tfw_pinref_t bad_bit  = { 0u, 32u };

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_tone_start(0, 440u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&bad_port, 440u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&bad_bit, 440u));
    TEST_ASSERT_EQUAL(0u, tfw_mcxn_model_log_len());
}

/* Codex P1 on #392: TONE on the tone pin, a GPIO mode command on that pin (its
 * PCR back to GPIO, as tfw_hal_gpio_mode writes it), TONE on it again: the mux must
 * be the timer's again, not left on GPIO under a cached pin. */
static void test_a_start_after_gpio_mode_restores_the_mux(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 440u));
    tfw_mcxn_model_set(A_PCR(3u, 19u), TFW_MCXN_PCR_IBE);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin5, 440u));
    TEST_ASSERT_EQUAL_HEX32(TONE_MUX, tfw_mcxn_model_get(A_PCR(3u, 19u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CEN, tfw_mcxn_model_get(A_TCR));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_start_clocks_then_programs_held_in_reset);
    RUN_TEST(test_divider_at_fro12m);
    RUN_TEST(test_divider_refuses_out_of_range);
    RUN_TEST(test_a_later_start_leaves_the_clock_alone);
    RUN_TEST(test_hz_0_stops);
    RUN_TEST(test_retarget_frequency_keeps_the_pin);
    RUN_TEST(test_retarget_pin_releases_the_old_one);
    RUN_TEST(test_stop_leaves_the_output_low);
    RUN_TEST(test_a_bad_pin_is_refused_with_no_register_written);
    RUN_TEST(test_a_start_after_gpio_mode_restores_the_mux);
    return UNITY_END();
}
