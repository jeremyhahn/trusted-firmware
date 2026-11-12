/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x CTIMER PWM (src/hal/cortex-m/mcxn94x/ctimer_pwm.c): the period
 * and prescale arithmetic. Not run on the board (docs/boards/frdm-mcxn947.md).
 */
#include <stdint.h>

#include "unity.h"
#include "ctimer_pwm.h"

#define FRO12M 12000000u

void setUp(void) {}
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_1khz_from_fro12m_is_pr_46_mr3_254);
    return UNITY_END();
}
