/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l tone: the TIMER arithmetic (tone_calc.h).
 */
#include <stdint.h>

#include "unity.h"
#include "tone_calc.h"

void setUp(void) {}
void tearDown(void) {}

/* 1 kHz: 16 MHz / (2 * 1000) = 8000 ticks per half period, prescaler 0. */
static void test_tone_calc_1khz(void)
{
    uint32_t pre = 99u, cc = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_nrf54l_tone_calc(1000u, &pre, &cc));
    TEST_ASSERT_EQUAL_UINT32(0u, pre);
    TEST_ASSERT_EQUAL_UINT32(8000u, cc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tone_calc_1khz);
    return UNITY_END();
}
