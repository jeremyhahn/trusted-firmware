/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stm32h5 tone HAL (src/hal/cortex-m/stm32h5/hal_tone.c) against a
 * register model: every TFW_CM_REG access goes to a slot table here, and the
 * order of first and last access to each register is logged, so the start
 * sequence can be checked in order. hal_tone.c is included unchanged.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#define TFW_CM_REGS_H   /* the port's cm_regs.h is replaced by the model */

#define MODEL_SLOTS 32u
#define MODEL_LOG   256u

static struct { uintptr_t addr; uint32_t val; } slot[MODEL_SLOTS];
static uintptr_t access_log[MODEL_LOG];
static unsigned  n_slots;
static unsigned  n_log;

static volatile uint32_t *model_reg(uintptr_t addr)
{
    unsigned i;

    if (n_log < MODEL_LOG) {
        access_log[n_log++] = addr;
    }
    for (i = 0u; i < n_slots; i++) {
        if (slot[i].addr == addr) {
            return &slot[i].val;
        }
    }
    TEST_ASSERT_TRUE(n_slots < MODEL_SLOTS);
    slot[n_slots].addr = addr;
    slot[n_slots].val  = 0u;
    return &slot[n_slots++].val;
}

#define TFW_CM_REG(addr) (*model_reg((uintptr_t)(addr)))

static uint32_t tone_tim_hz = 64000000u;
#define TFW_H5_TONE_TIM_HZ tone_tim_hz

#include "hal_tone.c"

#define GPIOB      (TFW_H5_GPIOA_BASE + TFW_H5_GPIO_STRIDE)
#define A_RCC_GPIO (TFW_H5_RCC_BASE + 0x8Cu)
#define A_RCC_TIM  (TFW_H5_RCC_BASE + 0x9Cu)
#define A_CR1      (TFW_H5_TONE_TIM_BASE + 0x00u)
#define A_EGR      (TFW_H5_TONE_TIM_BASE + 0x14u)
#define A_CCMR1    (TFW_H5_TONE_TIM_BASE + 0x18u)
#define A_CCER     (TFW_H5_TONE_TIM_BASE + 0x20u)
#define A_PSC      (TFW_H5_TONE_TIM_BASE + 0x28u)
#define A_ARR      (TFW_H5_TONE_TIM_BASE + 0x2Cu)
#define A_AFRL     (GPIOB + 0x20u)
#define A_MODER    (GPIOB + 0x00u)

static const tfw_pinref_t d3 = { .port = 1u, .bit = 5u };

static uint32_t peek(uintptr_t addr)
{
    unsigned i;

    for (i = 0u; i < n_slots; i++) {
        if (slot[i].addr == addr) {
            return slot[i].val;
        }
    }
    return 0u;
}

static void poke(uintptr_t addr, uint32_t v)
{
    *model_reg(addr) = v;
}

static int first(uintptr_t addr)
{
    unsigned i;

    for (i = 0u; i < n_log; i++) {
        if (access_log[i] == addr) {
            return (int)i;
        }
    }
    return -1;
}

static int last(uintptr_t addr)
{
    int i;

    for (i = (int)n_log - 1; i >= 0; i--) {
        if (access_log[i] == addr) {
            return i;
        }
    }
    return -1;
}

void setUp(void)
{
    memset(slot, 0, sizeof slot);
    n_slots = 0u;
    running = 0u;
    tone_tim_hz = 64000000u;
    poke(A_MODER, 0xFFFFFFFFu);        /* every pin analog, the reset state */
    poke(A_CCMR1, 0x00000068u);        /* channel 1's PWM mode, not ours */
    n_log = 0u;
}

void tearDown(void) {}

static void test_start_programs_the_divider_and_toggle_mode(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&d3, 1000u));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(A_PSC));
    TEST_ASSERT_EQUAL_HEX32(31999u, peek(A_ARR));
    TEST_ASSERT_EQUAL_HEX32(0x3068u, peek(A_CCMR1));
    TEST_ASSERT_EQUAL_HEX32(0x10u, peek(A_CCER));
    TEST_ASSERT_EQUAL_HEX32(1u, peek(A_EGR));
    TEST_ASSERT_EQUAL_HEX32(1u, peek(A_CR1) & 1u);
    TEST_ASSERT_EQUAL_HEX32(0x2u, peek(A_RCC_TIM));
    TEST_ASSERT_EQUAL_HEX32(0x2u, peek(A_RCC_GPIO));
    TEST_ASSERT_EQUAL_HEX32(0x2u << 20, peek(A_AFRL));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFBFFu, peek(A_MODER));
}

static void test_start_runs_in_order(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&d3, 440u));
    TEST_ASSERT_TRUE(first(A_RCC_TIM) < first(A_CR1));
    TEST_ASSERT_TRUE(first(A_CR1) < first(A_PSC));     /* halted before the reload */
    TEST_ASSERT_TRUE(first(A_ARR) < first(A_EGR));     /* UG after the reload */
    TEST_ASSERT_TRUE(first(A_EGR) < first(A_CCMR1));
    TEST_ASSERT_TRUE(last(A_AFRL) < first(A_MODER));   /* AF before the mode */
    TEST_ASSERT_TRUE(last(A_MODER) < last(A_CR1));     /* the counter starts last */
    TEST_ASSERT_TRUE(last(A_CCER) < last(A_CR1));
}

static void test_a_second_start_retargets(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&d3, 1000u));
    n_log = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&d3, 440u));
    TEST_ASSERT_EQUAL_HEX32(1u, peek(A_PSC));
    TEST_ASSERT_EQUAL_HEX32(36363u, peek(A_ARR));
    TEST_ASSERT_EQUAL_HEX32(0x3068u, peek(A_CCMR1));
    TEST_ASSERT_EQUAL_HEX32(1u, peek(A_CR1) & 1u);
    TEST_ASSERT_TRUE(first(A_CR1) < first(A_PSC));
}

static void test_stop_halts_and_holds_the_pin_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&d3, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_HEX32(0x4068u, peek(A_CCMR1));   /* forced inactive */
    TEST_ASSERT_EQUAL_HEX32(0u, peek(A_CR1) & 1u);
    TEST_ASSERT_EQUAL_HEX32(0x10u, peek(A_CCER));      /* still driving, low */
    n_log = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_stop());    /* idle: touches nothing */
    TEST_ASSERT_EQUAL(0u, n_log);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&d3, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tone_start(&d3, 0u)); /* 0 Hz is stop */
    TEST_ASSERT_EQUAL_HEX32(0u, peek(A_CR1) & 1u);
}

static void test_a_rate_the_timer_cannot_reach_is_refused_untouched(void)
{
    static const tfw_pinref_t other = { .port = 1u, .bit = 6u };

    tone_tim_hz = 100000u;   /* 60 kHz is one count a match at this clock */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_tone_start(&d3, 60000u));
    TEST_ASSERT_EQUAL(0u, n_log);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&other, 1000u));
    TEST_ASSERT_EQUAL(0u, n_log);
    TEST_ASSERT_EQUAL_UINT32(TFW_CM_CPU_HZ, 64000000u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_programs_the_divider_and_toggle_mode);
    RUN_TEST(test_start_runs_in_order);
    RUN_TEST(test_a_second_start_retargets);
    RUN_TEST(test_stop_halts_and_holds_the_pin_low);
    RUN_TEST(test_a_rate_the_timer_cannot_reach_is_refused_untouched);
    return UNITY_END();
}
