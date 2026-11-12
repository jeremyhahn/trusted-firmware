/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 software PWM (src/hal/cortex-m/mps2/hal_pwm.c) against a model
 * CMSDK dual timer and a model pin. test/model/cortex-m/mps2_pwm_model.h
 * stands in for the register layer, so hal_pwm.c compiles unchanged when it
 * is included below.
 *
 * The model timer is the registers only: a test plays the timer's interrupt by
 * calling the handler with Timer1MIS set, as QEMU's hw/timer/
 * cmsdk-apb-dualtimer.c raises it when the counter reaches zero.
 */
#include <stdint.h>
#include <string.h>

#include "../model/cortex-m/mps2_pwm_model.h"

#include "unity.h"
#include "tfw/board.h"
#include "tfw/hal.h"
#include "../../src/hal/cortex-m/mps2/hal_pwm.c"

#define DT_LOAD    (s_dt[0x00u / 4u])
#define DT_CONTROL (s_dt[0x08u / 4u])
#define DT_INTCLR  (s_dt[0x0Cu / 4u])
#define DT_MIS     (s_dt[0x14u / 4u])

/* The model pins: LED0 and LED1's level, and how many writes each saw. */
static uint8_t s_level[2];
static uint8_t s_writes[2];

static volatile uint32_t *model_reg(uintptr_t addr)
{
    if (addr >= (uintptr_t)TFW_MPS2_DUALTIMER_BASE
        && addr < (uintptr_t)TFW_MPS2_DUALTIMER_BASE + sizeof s_dt) {
        return &s_dt[(addr - (uintptr_t)TFW_MPS2_DUALTIMER_BASE) / 4u];
    }
    return &s_scratch;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *p, uint8_t level)
{
    if (p->port != 0u || p->bit > 1u) {
        return TFW_ERR_BAD_PIN;
    }
    s_level[p->bit] = level ? 1u : 0u;
    s_writes[p->bit]++;
    return TFW_OK;
}

static tfw_pwmref_t ch(uint8_t pin)
{
    tfw_pwmref_t r;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pwm(pin, &r));
    return r;
}

/* The timer's interrupt: the counter reached zero. */
static void tick(void)
{
    DT_MIS = 1u;
    TFW_MPS2_IRQ_HANDLER(TFW_MPS2_DUALTIMER_IRQ)();
    TEST_ASSERT_EQUAL_UINT32(1u, DT_INTCLR);
    DT_INTCLR = 0u;
    DT_MIS = 0u;
}

static const uint32_t STEP = TFW_MPS2_PWM_STEP;

void setUp(void)
{
    memset(s_dt, 0, sizeof s_dt);
    memset(s_level, 0, sizeof s_level);
    memset(s_writes, 0, sizeof s_writes);
    s_nvic_on = s_nvic_off = ~0u;
    memset(&s_pwm, 0, sizeof s_pwm);
}

void tearDown(void) {}

void test_pwm_duty_128_compare(void)
{
    tfw_pwmref_t r = ch(0u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&r, 128u));
    TEST_ASSERT_EQUAL_UINT32(TFW_MPS2_DUALTIMER_IRQ, s_nvic_on);
    TEST_ASSERT_BITS(TFW_MPS2_DT_CTRL_RUN, TFW_MPS2_DT_CTRL_RUN, DT_CONTROL);
    /* The period starts with the pin high and the compare 128 steps out. */
    TEST_ASSERT_EQUAL_UINT8(1u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT32(128u * STEP, DT_LOAD);
    tick();
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT32(128u * STEP, DT_LOAD);
    tick();
    TEST_ASSERT_EQUAL_UINT8(1u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT32(128u * STEP, DT_LOAD);
}

void test_pwm_compare_for_several_duties(void)
{
    static const uint8_t duties[] = { 1u, 64u, 200u, 254u };
    unsigned i;

    for (i = 0u; i < sizeof duties; i++) {
        tfw_pwmref_t r = ch(1u);

        setUp();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&r, duties[i]));
        TEST_ASSERT_EQUAL_UINT8(1u, s_level[1]);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)duties[i] * STEP, DT_LOAD);
        tick();
        TEST_ASSERT_EQUAL_UINT8(0u, s_level[1]);
        TEST_ASSERT_EQUAL_UINT32((256u - duties[i]) * STEP, DT_LOAD);
        tick();
        TEST_ASSERT_EQUAL_UINT8(1u, s_level[1]);
    }
}

void test_pwm_255_drives_high_and_leaves_the_timer(void)
{
    tfw_pwmref_t r = ch(0u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&r, 255u));
    TEST_ASSERT_EQUAL_UINT8(1u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, DT_CONTROL);
    TEST_ASSERT_EQUAL_UINT32(0u, DT_LOAD);
    TEST_ASSERT_EQUAL_UINT32(~0u, s_nvic_on);
}

void test_pwm_0_drives_low_and_leaves_the_timer(void)
{
    tfw_pwmref_t r = ch(0u);

    s_level[0] = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&r, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, DT_CONTROL);
    TEST_ASSERT_EQUAL_UINT32(~0u, s_nvic_on);
}

void test_pwm_two_channels(void)
{
    tfw_pwmref_t a = ch(0u);
    tfw_pwmref_t b = ch(1u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&a, 64u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&b, 192u));
    /* A second write lands at the next period start; play to it. */
    while (!(s_level[0] && s_level[1])) {
        tick();
    }
    TEST_ASSERT_EQUAL_UINT32(64u * STEP, DT_LOAD);
    tick();
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, s_level[1]);
    TEST_ASSERT_EQUAL_UINT32(128u * STEP, DT_LOAD);
    tick();
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[1]);
    TEST_ASSERT_EQUAL_UINT32(64u * STEP, DT_LOAD);
    tick();
    TEST_ASSERT_EQUAL_UINT8(1u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, s_level[1]);
}

void test_pwm_two_channels_same_duty(void)
{
    tfw_pwmref_t a = ch(0u);
    tfw_pwmref_t b = ch(1u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&a, 100u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&b, 100u));
    while (!(s_level[0] && s_level[1])) {
        tick();
    }
    TEST_ASSERT_EQUAL_UINT32(100u * STEP, DT_LOAD);
    tick();
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[1]);
    TEST_ASSERT_EQUAL_UINT32(156u * STEP, DT_LOAD);
}

void test_pwm_stop_one_keeps_the_other_running(void)
{
    tfw_pwmref_t a = ch(0u);
    tfw_pwmref_t b = ch(1u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&a, 64u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&b, 192u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&a));
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[0]);
    TEST_ASSERT_BITS(TFW_MPS2_DT_CTRL_RUN, TFW_MPS2_DT_CTRL_RUN, DT_CONTROL);
    TEST_ASSERT_EQUAL_UINT32(~0u, s_nvic_off);
    tick();
    tick();
    tick();
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[0]);
}

void test_pwm_stop_last_stops_the_timer(void)
{
    tfw_pwmref_t r = ch(0u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&r, 128u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&r));
    TEST_ASSERT_EQUAL_UINT8(0u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, DT_CONTROL & TFW_MPS2_DT_CTRL_EN);
    TEST_ASSERT_EQUAL_UINT32(TFW_MPS2_DUALTIMER_IRQ, s_nvic_off);
}

void test_pwm_255_after_running_stops_the_timer(void)
{
    tfw_pwmref_t r = ch(0u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&r, 128u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&r, 255u));
    TEST_ASSERT_EQUAL_UINT8(1u, s_level[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, DT_CONTROL & TFW_MPS2_DT_CTRL_EN);
    tick();
    TEST_ASSERT_EQUAL_UINT8(1u, s_level[0]);
}

void test_pwm_out_of_table_pin(void)
{
    tfw_pwmref_t r;
    tfw_pwmref_t bad = ch(0u);

    /* Pin 2, the SPI select, has no PWM row; pin 3 does not exist. */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(2u, &r));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pwm(TFW_PIN_COUNT, &r));
    /* A reference no table row made is refused by the HAL too. */
    bad.channel = TFW_PWM_COUNT;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_write(&bad, 128u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_stop(&bad));
    TEST_ASSERT_EQUAL_UINT32(0u, DT_CONTROL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pwm_duty_128_compare);
    RUN_TEST(test_pwm_compare_for_several_duties);
    RUN_TEST(test_pwm_255_drives_high_and_leaves_the_timer);
    RUN_TEST(test_pwm_0_drives_low_and_leaves_the_timer);
    RUN_TEST(test_pwm_two_channels);
    RUN_TEST(test_pwm_two_channels_same_duty);
    RUN_TEST(test_pwm_stop_one_keeps_the_other_running);
    RUN_TEST(test_pwm_stop_last_stops_the_timer);
    RUN_TEST(test_pwm_255_after_running_stops_the_timer);
    RUN_TEST(test_pwm_out_of_table_pin);
    return UNITY_END();
}
