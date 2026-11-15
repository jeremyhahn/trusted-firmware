/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 tone (row rp2350): the alarm-step arithmetic of soc_calc.h, and
 * src/hal/cortex-m/rp2350/hal_tone.c driven against a register model of
 * TIMER1, TICKS, RESETS, IO_BANK0, the pads and SIO
 * (test/model/cortex-m/rp2350/tone/tone_model.h).
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_calc.h"
#include "tone_model.h"

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

static void test_irq_late_by_periods_rearms_ahead(void)
{
    tfw_rp_model_set_time(0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));

    /* the interrupt runs 2100 us late, past four more targets: the next
     * target is the first one still ahead, on the 500 us grid */
    tfw_rp_model_set_time(2600u);
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(3000u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_rp_model_read(RP_MODEL_T1_ARMED));

    /* a target the timer sits on exactly has passed too */
    tfw_rp_model_set_time(3500u);
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(4000u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
}

static void test_irq_held_off_by_a_flash_write_rearms_ahead(void)
{
    tfw_rp_model_set_time(0xFFFF0000u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));

    /* an EEPROM write masks interrupts for 45.4 ms across the wrap */
    tfw_rp_model_set_time(0xFFFF0000u + 500u + 45400u);
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(BIT7, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(0xFFFF0000u + 46000u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));

    tfw_rp_model_set_time(0xFFFF0000u + 46000u);
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(0xFFFF0000u + 46500u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
}

static void test_irq_target_passing_during_the_alarm_write_rearms_again(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));

    /* the next target, 1000, is 1 us ahead when read; the ALARM0 write
     * takes 2 us, so TIMELR is at 1001 as the alarm arms: it fires and
     * pends IRQ4 again, and that second delivery finds no alarm */
    tfw_rp_model_set_time(999u);
    tfw_rp_model_slow_alarm_writes(2u, 1);
    TEST_ASSERT_EQUAL_INT(2, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(BIT7, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(1001u, tfw_rp_model_read(RP_MODEL_T1_TIMELR));
    TEST_ASSERT_EQUAL_UINT32(1500u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_rp_model_read(RP_MODEL_T1_ARMED) & 1u);

    /* two writes each slower than a half-period: the retry passes too,
     * and the third write lands ahead; the pin toggles once more */
    tfw_rp_model_set_time(1499u);
    tfw_rp_model_slow_alarm_writes(600u, 2);
    TEST_ASSERT_EQUAL_INT(2, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(2699u, tfw_rp_model_read(RP_MODEL_T1_TIMELR));
    TEST_ASSERT_EQUAL_UINT32(3000u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_rp_model_read(RP_MODEL_T1_ARMED) & 1u);
}

static void test_irq_alarm_fired_during_the_write_toggles_once(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 1000u));

    /* the ALARM0 write for target 1000 takes 5 us from 999: the alarm
     * fires as it arms, latches INTR and pends IRQ4 again in the NVIC.
     * The retry arms 1500 and clears INTR, but not the NVIC latch: the
     * handler is taken a second time and must find no alarm, so the pin
     * toggles once and the target stays 1500 */
    tfw_rp_model_set_time(999u);
    tfw_rp_model_slow_alarm_writes(5u, 1);
    TEST_ASSERT_EQUAL_INT(2, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_T1_INTR));
    TEST_ASSERT_EQUAL_UINT32(BIT7, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(1500u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_rp_model_read(RP_MODEL_T1_ARMED) & 1u);
}

static void test_start_preempted_past_the_first_target_arms_ahead(void)
{
    /* 25 kHz: a 20 us half-period, so the first target is 20. An interrupt
     * between the TIMELR read and the ALARM0 write (modelled as a 30 us
     * write) leaves TIMELR at 30, past it: the start arms 40 instead */
    tfw_rp_model_set_time(0u);
    tfw_rp_model_slow_alarm_writes(30u, 1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&pin7, 25000u));
    TEST_ASSERT_EQUAL_UINT32(30u, tfw_rp_model_read(RP_MODEL_T1_TIMELR));
    TEST_ASSERT_EQUAL_UINT32(40u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_rp_model_read(RP_MODEL_T1_ARMED) & 1u);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp_model_read(RP_MODEL_T1_INTR));

    /* and the tone runs: the alarm at 40 toggles the pin */
    tfw_rp_model_set_time(40u);
    TEST_ASSERT_EQUAL_INT(1, tfw_rp_model_fire());
    TEST_ASSERT_EQUAL_UINT32(BIT7, tfw_rp_model_read(RP_MODEL_SIO_OUT) & BIT7);
    TEST_ASSERT_EQUAL_UINT32(60u, tfw_rp_model_read(RP_MODEL_T1_ALARM0));
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
    RUN_TEST(test_irq_late_by_periods_rearms_ahead);
    RUN_TEST(test_irq_held_off_by_a_flash_write_rearms_ahead);
    RUN_TEST(test_irq_target_passing_during_the_alarm_write_rearms_again);
    RUN_TEST(test_irq_alarm_fired_during_the_write_toggles_once);
    RUN_TEST(test_start_preempted_past_the_first_target_arms_ahead);
    RUN_TEST(test_retarget_drives_old_pin_low);
    RUN_TEST(test_retarget_same_pin_new_step);
    RUN_TEST(test_stop);
    return UNITY_END();
}
