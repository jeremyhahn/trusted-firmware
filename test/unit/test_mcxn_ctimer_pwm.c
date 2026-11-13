/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x CTIMER PWM (src/hal/cortex-m/mcxn94x/ctimer_pwm.c): the period
 * and prescale arithmetic, and the match-register sequence on a register
 * model of one CTIMER. Not run on the board (docs/boards/frdm-mcxn947.md).
 */
#include <stdint.h>

#include "unity.h"
#include "ctimer_pwm.h"

#define FRO12M 12000000u
#define FROHF  48000000u

static volatile uint32_t ct[TFW_MCXN_CT_WORDS];

void setUp(void)
{
    for (uint32_t i = 0u; i < TFW_MCXN_CT_WORDS; i++) {
        ct[i] = 0u;
    }
}
void tearDown(void) {}

/* 12 MHz / (47 * 255) = 1001.25 Hz: PR 46, MR3 254. */
static void test_1khz_from_fro12m_is_pr_46_mr3_254(void)
{
    uint32_t pr = 0xFFFFFFFFu;
    uint32_t mr3 = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_period(FRO12M, &pr, &mr3));
    TEST_ASSERT_EQUAL_UINT32(46u, pr);
    TEST_ASSERT_EQUAL_UINT32(254u, mr3);
}

/* 48 MHz / (188 * 255) = 1001.25 Hz. */
static void test_1khz_from_frohf_is_pr_187(void)
{
    uint32_t pr = 0u;
    uint32_t mr3 = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_period(FROHF, &pr, &mr3));
    TEST_ASSERT_EQUAL_UINT32(187u, pr);
    TEST_ASSERT_EQUAL_UINT32(254u, mr3);
}

/* 255 kHz is one count a clock: PR 0. Below it the rate is out of reach. */
static void test_the_slowest_clock_is_255khz(void)
{
    uint32_t pr = 1u;
    uint32_t mr3 = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_period(255000u, &pr, &mr3));
    TEST_ASSERT_EQUAL_UINT32(0u, pr);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, mcxn_ctimer_pwm_period(254999u, &pr, &mr3));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, mcxn_ctimer_pwm_period(0u, &pr, &mr3));
}

static void test_start_programs_the_period_with_every_channel_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_start(ct, FRO12M));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CEN, ct[TFW_MCXN_CT_TCR]);
    TEST_ASSERT_EQUAL_UINT32(0u, ct[TFW_MCXN_CT_CTCR]);
    TEST_ASSERT_EQUAL_UINT32(46u, ct[TFW_MCXN_CT_PR]);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_MCR_MR3R, ct[TFW_MCXN_CT_MCR]);
    TEST_ASSERT_EQUAL_UINT32(254u, ct[TFW_MCXN_CT_MR(3u)]);
    TEST_ASSERT_EQUAL_UINT32(0u, ct[TFW_MCXN_CT_PWMC]);
    for (uint8_t n = 0u; n < 3u; n++) {
        TEST_ASSERT_EQUAL_UINT32(255u, ct[TFW_MCXN_CT_MR(n)]);
    }
}

static void test_start_refuses_a_slow_clock_and_leaves_the_timer_alone(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, mcxn_ctimer_pwm_start(ct, 1000u));
    TEST_ASSERT_EQUAL_UINT32(0u, ct[TFW_MCXN_CT_TCR]);
}

/* The output is low until MRn: duty 0 never matches (MRn 255 > MR3 254), duty
 * 255 matches at 0 and is high all cycle, duty 128 is high for 128 of 255. */
static void test_duty_0_128_255_on_each_channel(void)
{
    static const uint8_t duty[] = { 0u, 128u, 255u };
    static const uint32_t match[] = { 255u, 127u, 0u };

    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_start(ct, FRO12M));
    for (uint8_t ch = 0u; ch < 3u; ch++) {
        for (uint8_t i = 0u; i < 3u; i++) {
            TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_duty(ct, ch, duty[i]));
            TEST_ASSERT_EQUAL_UINT32(match[i], ct[TFW_MCXN_CT_MR(ch)]);
            /* high counts: MR3 + 1 - MRn, the duty itself */
            TEST_ASSERT_EQUAL_UINT32(duty[i], 254u + 1u - ct[TFW_MCXN_CT_MR(ch)]);
            TEST_ASSERT_TRUE(ct[TFW_MCXN_CT_PWMC] & (1u << ch));
        }
    }
    TEST_ASSERT_EQUAL_HEX32(0x7u, ct[TFW_MCXN_CT_PWMC]);
    TEST_ASSERT_EQUAL_UINT32(254u, ct[TFW_MCXN_CT_MR(3u)]);
}

static void test_match_3_is_not_a_duty_channel(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_start(ct, FRO12M));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, mcxn_ctimer_pwm_duty(ct, 3u, 10u));
    TEST_ASSERT_EQUAL_UINT32(254u, ct[TFW_MCXN_CT_MR(3u)]);
    TEST_ASSERT_EQUAL_UINT32(0u, ct[TFW_MCXN_CT_PWMC]);
}

/* Stop: the channel leaves PWM mode; the counter stops with the last one. */
static void test_halt_stops_the_counter_with_the_last_channel(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_start(ct, FRO12M));
    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_duty(ct, 0u, 100u));
    TEST_ASSERT_EQUAL(TFW_OK, mcxn_ctimer_pwm_duty(ct, 2u, 200u));

    TEST_ASSERT_EQUAL_UINT8(0u, mcxn_ctimer_pwm_halt(ct, 2u));
    TEST_ASSERT_EQUAL_HEX32(0x1u, ct[TFW_MCXN_CT_PWMC]);
    TEST_ASSERT_EQUAL_UINT32(255u, ct[TFW_MCXN_CT_MR(2u)]);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CEN, ct[TFW_MCXN_CT_TCR]);

    TEST_ASSERT_EQUAL_UINT8(1u, mcxn_ctimer_pwm_halt(ct, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, ct[TFW_MCXN_CT_PWMC]);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CT_TCR_CRST, ct[TFW_MCXN_CT_TCR]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_1khz_from_fro12m_is_pr_46_mr3_254);
    RUN_TEST(test_1khz_from_frohf_is_pr_187);
    RUN_TEST(test_the_slowest_clock_is_255khz);
    RUN_TEST(test_start_programs_the_period_with_every_channel_low);
    RUN_TEST(test_start_refuses_a_slow_clock_and_leaves_the_timer_alone);
    RUN_TEST(test_duty_0_128_255_on_each_channel);
    RUN_TEST(test_match_3_is_not_a_duty_channel);
    RUN_TEST(test_halt_stops_the_counter_with_the_last_channel);
    return UNITY_END();
}
