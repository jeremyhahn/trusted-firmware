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
 *
 * The same header's ADC sample-time field (tfw_h5_adc_smpr) and PWM CCER off
 * branch (tfw_h5_pwm_ccer) are pinned here too, from the RM0481 bit layouts.
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

static void test_a_part_megahertz_is_dropped_before_the_multiply(void)
{
    /* 64.999999 MHz is 64 whole MHz: 64 * 10000 us = 640000 spins. The
     * 0.999999 MHz is dropped, not rounded and not carried into the product. */
    TEST_ASSERT_EQUAL_UINT32(640000u, tfw_h5_spins_us(64999999u, TFW_H5_ADC_POLL_US));
    /* 250.999999 MHz is 250 whole MHz: 250 * 20 us = 5000 spins. */
    TEST_ASSERT_EQUAL_UINT32(5000u, tfw_h5_spins_us(250999999u, TFW_H5_ADC_REGUL_US));
}

static void test_one_megahertz_gives_one_spin_per_us_and_zero_us_none(void)
{
    /* 1 MHz * 10000 us = 10000 spins: one spin a microsecond. */
    TEST_ASSERT_EQUAL_UINT32(10000u, tfw_h5_spins_us(1000000u, TFW_H5_ADC_POLL_US));
    /* A zero-microsecond wait is no spins, whatever the clock. */
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_h5_spins_us(64000000u, 0u));
}

static void test_an_adc_sample_time_keeps_three_bits_and_channel_19_tops_smpr2(void)
{
    /* Channel 19 is SMPR2's tenth field: 19 % 10 = 9, shift 27, bits 29:27. */
    TEST_ASSERT_EQUAL_HEX32(0x38000000u, tfw_h5_adc_smpr(0u, 19u, 7u));
    /* Writing 0 there clears bits 29:27 and keeps every other bit. */
    TEST_ASSERT_EQUAL_HEX32(0xC7FFFFFFu, tfw_h5_adc_smpr(0xFFFFFFFFu, 19u, 0u));
    /* 0xF is masked to 7: bit 3 does not spill into channel 1's field. */
    TEST_ASSERT_EQUAL_HEX32(0x00000007u, tfw_h5_adc_smpr(0u, 0u, 0xFu));
    /* 8 is masked to 0: nothing lands in channel 2's field. */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, tfw_h5_adc_smpr(0u, 1u, 8u));
}

static void test_a_pwm_channel_turned_off_clears_its_enable_and_polarity_only(void)
{
    /* Channel 1 off: CC1E (bit 0) and CC1P (bit 1) cleared, CC1NE and the rest kept. */
    TEST_ASSERT_EQUAL_HEX32(0xFFFCu, tfw_h5_pwm_ccer(0xFFFFu, 1u, 0u));
    /* Channel 4 off: CC4E and CC4P, bits 13:12, cleared. */
    TEST_ASSERT_EQUAL_HEX32(0xCFFFu, tfw_h5_pwm_ccer(0xFFFFu, 4u, 0u));
    /* Channel 2 off: only CC2E goes; channels 1, 3 and 4 stay on. */
    TEST_ASSERT_EQUAL_HEX32(0x1101u, tfw_h5_pwm_ccer(0x1111u, 2u, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_adc_poll_bound_is_10_ms_of_core_cycles);
    RUN_TEST(test_spins_do_not_overflow_at_the_widest_products);
    RUN_TEST(test_a_part_megahertz_is_dropped_before_the_multiply);
    RUN_TEST(test_one_megahertz_gives_one_spin_per_us_and_zero_us_none);
    RUN_TEST(test_an_adc_sample_time_keeps_three_bits_and_channel_19_tops_smpr2);
    RUN_TEST(test_a_pwm_channel_turned_off_clears_its_enable_and_polarity_only);
    return UNITY_END();
}
