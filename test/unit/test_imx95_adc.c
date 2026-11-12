/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The i.MX 95 M7's SAR ADC (src/hal/cortex-m/imx95/hal_adc.c) against a
 * register model of the block, since QEMU has no i.MX 95 (SPEC row `imx95`).
 *
 * The driver reaches its registers the way the SoC's other HAL files do,
 * through TFW_CM_REG, which test/model/cortex-m/cm_regs.h sends to
 * tfw_imx95_model_reg(). This file defines that function itself, for the ADC
 * alone (test/model/imx95_model.c is not linked): the block's registers live
 * in an array here, and each call first settles what the previous access set
 * in motion (power-down, calibration, a conversion), as a lazy model must,
 * since C gives an lvalue no hook on a read or a write.
 *
 * Built twice: test_imx95_adc with a block at TEST_ADC_BASE, and
 * test_imx95_adc_nobase with TFW_IMX95_ADC_BASE zero, where no register may
 * be touched.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "imx95_model.h"
#include "sar_adc.h"

#define TEST_ADC_BASE 0x40AD0000uL
#define NREGS (0x400u / 4u)

enum cal_mode { CAL_PASS, CAL_FAIL, CAL_HANG };

static uint32_t regs[NREGS];
static uint32_t other;              /* any address outside the block */
static unsigned touches;            /* every call, in or out of the block */
static unsigned out_of_block;
static enum cal_mode cal;
static unsigned cal_runs;
static int conv_hang;
static unsigned conversions;
static uint16_t sample[TFW_IMX95_ADC_CHANNELS];

#define R(off) regs[(off) / 4u]

static void settle(void)
{
    uint32_t mcr = R(TFW_IMX95_ADC_MCR);
    uint32_t msr = R(TFW_IMX95_ADC_MSR) & ~TFW_IMX95_ADC_MSR_ADCSTATUS;

    msr |= (mcr & TFW_IMX95_ADC_MCR_PWDN) ? TFW_IMX95_ADC_STATUS_POWER_DOWN
                                          : TFW_IMX95_ADC_STATUS_IDLE;
    if ((mcr & TFW_IMX95_ADC_MCR_CALSTART) && !(mcr & TFW_IMX95_ADC_MCR_PWDN)) {
        if (cal == CAL_HANG) {
            msr |= TFW_IMX95_ADC_MSR_CALBUSY;
        } else {
            cal_runs++;
            mcr &= ~TFW_IMX95_ADC_MCR_CALSTART;
            msr &= ~(TFW_IMX95_ADC_MSR_CALBUSY | TFW_IMX95_ADC_MSR_CALFAIL);
            if (cal == CAL_FAIL) {
                msr |= TFW_IMX95_ADC_MSR_CALFAIL;
                R(TFW_IMX95_ADC_CALSTAT) = 0x00010000u;
            }
        }
    }
    if ((mcr & TFW_IMX95_ADC_MCR_NSTART) && !(mcr & TFW_IMX95_ADC_MCR_PWDN) &&
        !conv_hang) {
        uint32_t mask = R(TFW_IMX95_ADC_NCMR0);

        conversions++;
        if (!(mcr & TFW_IMX95_ADC_MCR_MODE)) {
            mcr &= ~TFW_IMX95_ADC_MCR_NSTART;       /* one-shot: the chain ends */
        }
        R(TFW_IMX95_ADC_CEOCFR0) = 0u;
        for (uint32_t ch = 0u; ch < TFW_IMX95_ADC_CHANNELS; ch++) {
            if (mask & (1uL << ch)) {
                /* bits above CDATA set, so a driver must mask them */
                R(TFW_IMX95_ADC_PCDR(ch)) = 0xFFFF0000uL | sample[ch];
                R(TFW_IMX95_ADC_CEOCFR0) |= 1uL << ch;
            }
        }
        R(TFW_IMX95_ADC_ISR) = TFW_IMX95_ADC_ISR_EOC | TFW_IMX95_ADC_ISR_ECH;
    }
    R(TFW_IMX95_ADC_MCR) = mcr;
    R(TFW_IMX95_ADC_MSR) = msr;
}

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    touches++;
    settle();
    if (addr >= TEST_ADC_BASE && addr < TEST_ADC_BASE + 4u * NREGS) {
        return (volatile uint32_t *)&regs[(addr - TEST_ADC_BASE) / 4u];
    }
    out_of_block++;
    return (volatile uint32_t *)&other;
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    /* out of reset the block is powered down (imx93_adc.c powers it up) */
    R(TFW_IMX95_ADC_MCR) = TFW_IMX95_ADC_MCR_PWDN | TFW_IMX95_ADC_MCR_ADCLKSE;
    R(TFW_IMX95_ADC_MSR) = TFW_IMX95_ADC_STATUS_POWER_DOWN;
    other = 0u;
    touches = 0u;
    out_of_block = 0u;
    cal = CAL_PASS;
    cal_runs = 0u;
    conv_hang = 0;
    conversions = 0u;
    for (unsigned ch = 0u; ch < TFW_IMX95_ADC_CHANNELS; ch++) {
        sample[ch] = (uint16_t)(0x100u * ch + 0x23u);
    }
}

void tearDown(void) {}

#if TFW_IMX95_ADC_BASE != 0

static void test_adc_read_channel0(void)
{
    uint16_t raw = 0u;

    sample[0] = 0x0ABCu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x0ABCu, raw);
    TEST_ASSERT_EQUAL_UINT(1u, conversions);
    TEST_ASSERT_EQUAL_UINT(0u, out_of_block);
}

static void test_init_powers_up_and_calibrates_once(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL_UINT(1u, cal_runs);
    TEST_ASSERT_FALSE(R(TFW_IMX95_ADC_MCR) & TFW_IMX95_ADC_MCR_PWDN);
    TEST_ASSERT_FALSE(R(TFW_IMX95_ADC_MCR) & TFW_IMX95_ADC_MCR_CALSTART);
}

static void test_a_read_on_each_channel(void)
{
    uint16_t raw;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    for (uint8_t ch = 0u; ch < TFW_ADC_COUNT; ch++) {
        sample[ch] = (uint16_t)(0x0F00u | (0x11u * ch));
        raw = 0xFFFFu;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(ch, &raw));
        TEST_ASSERT_EQUAL_HEX16(sample[ch], raw);
        /* one channel in the mask, one-shot mode */
        TEST_ASSERT_EQUAL_HEX32(1uL << ch, R(TFW_IMX95_ADC_NCMR0));
        TEST_ASSERT_FALSE(R(TFW_IMX95_ADC_MCR) & TFW_IMX95_ADC_MCR_MODE);
        /* polled: no interrupt unmasked */
        TEST_ASSERT_EQUAL_HEX32(0u, R(TFW_IMX95_ADC_IMR));
        TEST_ASSERT_EQUAL_HEX32(0u, R(TFW_IMX95_ADC_CIMR0));
    }
    TEST_ASSERT_EQUAL_UINT(TFW_ADC_COUNT, conversions);
}

static void test_a_channel_out_of_range(void)
{
    uint16_t raw = 0x5A5Au;
    unsigned before;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    before = touches;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read((uint8_t)TFW_ADC_COUNT, &raw));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(0xFFu, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, raw);
    TEST_ASSERT_EQUAL_UINT(before, touches);
}

static void test_a_calibration_that_never_completes(void)
{
    uint16_t raw = 0x5A5Au;
    unsigned before;

    cal = CAL_HANG;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
    /* init returned, so main() boots; every read answers the failure */
    cal = CAL_PASS;
    before = touches;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, raw);
    TEST_ASSERT_EQUAL_UINT(before, touches);
    TEST_ASSERT_EQUAL_UINT(0u, conversions);
}

static void test_a_failed_calibration_is_recorded(void)
{
    uint16_t raw = 0u;

    cal = CAL_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_UINT(0u, conversions);
}

static void test_a_second_init_after_a_failure_recovers(void)
{
    uint16_t raw = 0u;

    cal = CAL_HANG;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
    cal = CAL_PASS;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(1u, &raw));
    TEST_ASSERT_EQUAL_HEX16(sample[1], raw);
}

static void test_a_conversion_that_never_ends_times_out(void)
{
    uint16_t raw = 0x5A5Au;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    conv_hang = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, raw);
    /* the start is withdrawn, so the next read begins clean */
    TEST_ASSERT_FALSE(R(TFW_IMX95_ADC_MCR) & TFW_IMX95_ADC_MCR_NSTART);
    conv_hang = 0;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_HEX16(sample[0], raw);
}

static void test_the_references_accepted_and_refused(void)
{
    unsigned before = touches;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref((tfw_adc_ref_t)7));
    TEST_ASSERT_EQUAL_UINT(before, touches);
}

#else /* TFW_IMX95_ADC_BASE == 0 */

static void test_base_zero_touches_nothing(void)
{
    uint16_t raw = 0x5A5Au;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    for (uint8_t ch = 0u; ch < TFW_ADC_COUNT; ch++) {
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(ch, &raw));
    }
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(0xFFu, &raw));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, raw);
    TEST_ASSERT_EQUAL_UINT(0u, touches);
}

#endif

int main(void)
{
    UNITY_BEGIN();
#if TFW_IMX95_ADC_BASE != 0
    RUN_TEST(test_adc_read_channel0);
    RUN_TEST(test_init_powers_up_and_calibrates_once);
    RUN_TEST(test_a_read_on_each_channel);
    RUN_TEST(test_a_channel_out_of_range);
    RUN_TEST(test_a_calibration_that_never_completes);
    RUN_TEST(test_a_failed_calibration_is_recorded);
    RUN_TEST(test_a_second_init_after_a_failure_recovers);
    RUN_TEST(test_a_conversion_that_never_ends_times_out);
    RUN_TEST(test_the_references_accepted_and_refused);
#else
    RUN_TEST(test_base_zero_touches_nothing);
#endif
    return UNITY_END();
}
