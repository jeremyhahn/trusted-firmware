/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

/* The i.MX 95 TPM period at the power-of-two boundary (#473): a period count
 * of exactly 0x10000 does not fit MOD's 16 bits, so it takes the next
 * prescaler, as Linux v6.12 pwm_imx_tpm_round_state does, and full duty's
 * CnV stays within 16 bits. */

#include "unity.h"

#include "../../src/hal/cortex-m/imx95/tpm.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_count_of_exactly_0x10000_takes_the_next_prescaler(void)
{
    uint32_t ps  = 99u;
    uint32_t mod = 99u;

    tfw_imx95_tpm_period(65536000u, 1000u, &ps, &mod);
    TEST_ASSERT_EQUAL_UINT32(1u, ps);
    TEST_ASSERT_EQUAL_UINT32(32767u, mod);

    tfw_imx95_tpm_period(131072000u, 1000u, &ps, &mod);
    TEST_ASSERT_EQUAL_UINT32(2u, ps);
    TEST_ASSERT_EQUAL_UINT32(32767u, mod);
}

static void test_a_count_of_0xffff_stays_at_ps_0(void)
{
    uint32_t ps  = 99u;
    uint32_t mod = 99u;

    tfw_imx95_tpm_period(65535000u, 1000u, &ps, &mod);
    TEST_ASSERT_EQUAL_UINT32(0u, ps);
    TEST_ASSERT_EQUAL_UINT32(65534u, mod);
}

static void test_full_duty_after_a_boundary_period_fits_16_bits(void)
{
    uint32_t ps  = 99u;
    uint32_t mod = 99u;
    uint32_t cnv;

    tfw_imx95_tpm_period(65536000u, 1000u, &ps, &mod);
    cnv = tfw_imx95_tpm_cnv(mod, 255u);
    TEST_ASSERT_TRUE(cnv <= 0x10000u);
    TEST_ASSERT_EQUAL_UINT32(32768u, cnv);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_count_of_exactly_0x10000_takes_the_next_prescaler);
    RUN_TEST(test_a_count_of_0xffff_stays_at_ps_0);
    RUN_TEST(test_full_duty_after_a_boundary_period_fits_16_bits);
    return UNITY_END();
}
