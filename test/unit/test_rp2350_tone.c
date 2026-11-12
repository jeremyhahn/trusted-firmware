/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 tone (row rp2350): the alarm-step arithmetic of soc_calc.h, and
 * src/hal/cortex-m/rp2350/hal_tone.c driven against a register model of
 * TIMER1, TICKS, RESETS, IO_BANK0, the pads and SIO
 * (test/model/cortex-m/rp2350/rp2350_model.h).
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_calc.h"
#include "rp2350_model.h"

#define GP7  7u
#define BIT7 (1u << GP7)
#define GP9  9u
#define BIT9 (1u << GP9)

static const tfw_pinref_t pin7 = { 0u, GP7 };
static const tfw_pinref_t pin9 = { 0u, GP9 };

void setUp(void)
{
    tfw_rp_model_reset();
    (void)tfw_hal_tone_stop();
    tfw_rp_model_reset();
}

void tearDown(void)
{
}

static void test_tone_calc_rounding(void)
{
    /* 1e6 / (2 * hz) us between toggles, rounded to nearest */
    TEST_ASSERT_EQUAL_UINT32(10000u, tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, 50u));
    TEST_ASSERT_EQUAL_UINT32(500u, tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, 1000u));
    TEST_ASSERT_EQUAL_UINT32(167u, tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, 3000u));
    TEST_ASSERT_EQUAL_UINT32(25u, tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, 20000u));
    TEST_ASSERT_EQUAL_UINT32(20u, tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, 25000u));
}

static void test_tone_calc_refuses(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, 30000u));
}

static void test_start_refuses_zero_and_out_of_range(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&pin7, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&pin7, 30000u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&pin7, 65535u));
    /* a refusal touches no register */
    TEST_ASSERT_EQUAL_INT(0, tfw_rp_model_writes());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_T1_INTE));
}

static void test_start_refuses_bad_pin(void)
{
    const tfw_pinref_t gp30 = { 0u, 30u };
    const tfw_pinref_t bank1 = { 1u, GP7 };

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&gp30, 1000u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&bank1, 1000u));
    TEST_ASSERT_EQUAL_INT(0, tfw_rp_model_writes());
}

static void test_start_ordering(void)
{
    int unreset, ticks, low, oe, func, alarm, inte, iser;

    tfw_rp_model_set_time(1000u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));

    /* TIMER1 out of reset (the CLR alias of RESET) and its 1 us tick */
    unreset = tfw_rp_model_find(RP_MODEL_RESETS_RESET + 0x3000u, RP_MODEL_RESET_TIMER1, 0);
    ticks   = tfw_rp_model_find(RP_MODEL_TICKS_T1_CTRL, 1u, 0);
    low     = tfw_rp_model_find(RP_MODEL_SIO_OUT_CLR, BIT7, 0);
    oe      = tfw_rp_model_find(RP_MODEL_SIO_OE_SET, BIT7, 0);
    func    = tfw_rp_model_find(RP_MODEL_IO_CTRL(GP7), 5u, 0);
    alarm   = tfw_rp_model_find(RP_MODEL_T1_ALARM0, 1500u, 0);
    inte    = tfw_rp_model_find(RP_MODEL_T1_INTE, 1u, 0);
    iser    = tfw_rp_model_find(RP_MODEL_NVIC_ISER0, 1u << 4, 0);
    TEST_ASSERT_TRUE(unreset >= 0);
    TEST_ASSERT_TRUE(unreset < ticks);
    TEST_ASSERT_TRUE(ticks < low);
    TEST_ASSERT_TRUE(low < oe);
    TEST_ASSERT_TRUE(oe < func);
    TEST_ASSERT_TRUE(func < alarm);
    TEST_ASSERT_TRUE(alarm < inte);
    TEST_ASSERT_TRUE(inte < iser);

    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_RESETS_RESET) & RP_MODEL_RESET_TIMER1);
    TEST_ASSERT_EQUAL_UINT32(12u, tfw_rp_model_read(RP_MODEL_TICKS_T1_CYC));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(BIT7, tfw_rp_model_read(RP_MODEL_SIO_OE) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_rp_model_read(RP_MODEL_T1_ARMED));
}

static void test_start_keeps_running_timer(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 2000u));
    /* the second start finds TIMER1 out of reset and ticking */
    TEST_ASSERT_EQUAL_INT(-1, tfw_rp_model_find(RP_MODEL_TICKS_T1_CYC, 12u, 1 +
        tfw_rp_model_find(RP_MODEL_TICKS_T1_CYC, 12u, 0)));
}

static void test_irq_toggles_and_rearms_from_target(void)
{
    tfw_rp_model_set_time(0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));
    TEST_ASSERT_EQUAL_UINT32(500u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));

    /* the interrupt runs 7 us late: the next target is still 500 on */
    tfw_rp_model_set_time(507u);
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_T1_INTR));
    TEST_ASSERT_EQUAL_UINT32(BIT7, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(1000u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_rp_model_read(RP_MODEL_T1_ARMED));

    tfw_rp_model_set_time(1000u);
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(1500u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
}

static void test_irq_rearm_wraps(void)
{
    tfw_rp_model_set_time(0xFFFFFF00u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 50u));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFF00u + 10000u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFF00u + 20000u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
}

static void test_retarget_drives_old_pin_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(BIT7, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);

    tfw_rp_model_set_time(2000u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin9, 20000u));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_SIO_OUT) & (BIT7 | BIT9));
    TEST_ASSERT_EQUAL_UINT32(2025u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));

    /* the interrupt now toggles the new pin only */
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(BIT9, tfw_rp_model_read(RP_MODEL_SIO_OUT) & (BIT7 | BIT9));
    TEST_ASSERT_EQUAL_UINT32(2050u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
}

static void test_retarget_same_pin_new_step(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));
    tfw_rp_model_set_time(300u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 50u));
    TEST_ASSERT_EQUAL_UINT32(10300u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(20300u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
}

static void test_stop(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(BIT7, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_T1_INTE));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_T1_ARMED));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_INT(0, tfw_rp_model_fire());

    /* a stop with nothing playing is harmless */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tone_calc_rounding);
    RUN_TEST(test_tone_calc_refuses);
    RUN_TEST(test_start_refuses_zero_and_out_of_range);
    RUN_TEST(test_start_refuses_bad_pin);
    RUN_TEST(test_start_ordering);
    RUN_TEST(test_start_keeps_running_timer);
    RUN_TEST(test_irq_toggles_and_rearms_from_target);
    RUN_TEST(test_irq_rearm_wraps);
    RUN_TEST(test_retarget_drives_old_pin_low);
    RUN_TEST(test_retarget_same_pin_new_step);
    RUN_TEST(test_stop);
    return UNITY_END();
}
