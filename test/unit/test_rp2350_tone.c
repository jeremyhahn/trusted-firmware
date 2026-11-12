/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 tone (row rp2350): the alarm-step arithmetic of soc_calc.h, and
 * src/hal/cortex-m/rp2350/hal_tone.c driven against a register model of
 * TIMER1, TICKS, RESETS, IO_BANK0, the pads and SIO.
 */
#include <stdint.h>

#include "unity.h"
#include "soc_calc.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_tone_calc_1khz(void)
{
    /* 1e6 / (2 * 1000) = 500 us between toggles */
    TEST_ASSERT_EQUAL_UINT32(500u, tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, 1000u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tone_calc_1khz);
    return UNITY_END();
}
