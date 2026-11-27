/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 HAL's busy-wait bound (tfw_h5_spins_us in
 * src/hal/cortex-m/stm32h5/h5_calc.h), checked on x86: the ADC bounds its
 * calibration, ADRDY and EOC polls with it at 10 ms of core cycles.
 *
 * The expected values are literals worked by hand, never from the code under
 * test: the clock in MHz times the microseconds. Dividing the clock to MHz
 * first keeps every product inside 32 bits; multiplying first would wrap
 * 64 MHz * 10000 us and leave the poll under a microsecond.
 */

#include <stdint.h>

#include "unity.h"
#include "../../src/hal/cortex-m/stm32h5/h5_calc.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_adc_poll_bound_is_10_ms_of_core_cycles(void)
{
    /* 64 MHz * 10000 us = 640000 spins. */
    TEST_ASSERT_EQUAL_UINT32(640000u, tfw_h5_spins_us(64000000u, TFW_H5_ADC_POLL_US));
    /* 64 MHz * 20 us = 1280 spins: the regulator start-up wait. */
    TEST_ASSERT_EQUAL_UINT32(1280u, tfw_h5_spins_us(64000000u, TFW_H5_ADC_REGUL_US));
}

static void test_spins_do_not_overflow_at_the_widest_products(void)
{
    /* The H5's 250 MHz top clock: 250 * 10000 = 2500000 spins. */
    TEST_ASSERT_EQUAL_UINT32(2500000u, tfw_h5_spins_us(250000000u, TFW_H5_ADC_POLL_US));
    /* 1 MHz * 0xFFFFFFFF us: the widest wait at one spin a microsecond. */
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, tfw_h5_spins_us(1000000u, 0xFFFFFFFFu));
    /* The widest clock, 4294 whole MHz, for one microsecond. */
    TEST_ASSERT_EQUAL_UINT32(4294u, tfw_h5_spins_us(0xFFFFFFFFu, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_adc_poll_bound_is_10_ms_of_core_cycles);
    RUN_TEST(test_spins_do_not_overflow_at_the_widest_products);
    return UNITY_END();
}
