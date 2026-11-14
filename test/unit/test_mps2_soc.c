/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mps2 SoC's watchdog code (src/hal/cortex-m/mps2/hal_soc.c: its early
 * disarm, arm and pet) on a host register file
 * (test/model/cortex-m/mps2_soc_model.c), compiled for mps2-an505 (20 MHz
 * APB), and the value each leaves in the CMSDK APB watchdog's registers,
 * checked against the watchdog's programmers model in the Cortex-M System
 * Design Kit TRM (Arm DDI 0479):
 *
 *   LOAD   +0x000  the count; a write reloads the counter
 *   CTRL   +0x008  bit 0 INTEN (also runs the counter), bit 1 RESEN
 *   INTCLR +0x00C  any write clears the interrupt and reloads the counter
 *   LOCK   +0xC00  0x1ACCE551 enables writes to the others, anything else
 *                  disables them
 *
 * So early disarms (CTRL 0), arm loads 4 s of APB clock and sets INTEN and
 * RESEN, pet writes INTCLR, and each unlocks first and locks again after.
 * The file ignores a write while LOCK is shut, as the watchdog does.
 *
 * These run on the host only. A board has not run them.
 */
#include <stdint.h>
#include "unity.h"
#include "tfw/config.h"
#include "hal_early.h"
#include "mps2_soc_model.h"

_Static_assert(TFW_CM_CPU_HZ == 20000000uL,
               "the suite is built for mps2-an505, whose APB runs at 20 MHz");

#define WDOG_KEY  0x1ACCE551u
#define LOAD_4S   80000000u /* 4 s at 20 MHz */
#define CTRL_INTEN 0x1u
#define CTRL_RESEN 0x2u

#define M (tfw_mps2_model)

/* A watchdog an earlier image left running: armed, locked, its interrupt
 * raised and the count nearly out. */
static void preset_running(void)
{
    M.load   = 1234u;
    M.value  = 7u;
    M.ctrl   = CTRL_INTEN | CTRL_RESEN;
    M.ris    = 1;
    M.locked = 1;
}

void setUp(void)
{
    tfw_mps2_model_reset();
}

void tearDown(void) {}

/* The first write opens LOCK with the key, the last shuts it, and nothing
 * between was ignored or went elsewhere. */
static void assert_bracketed(void)
{
    TEST_ASSERT_TRUE(M.n >= 2u);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MODEL_LOCK, M.log[0].off);
    TEST_ASSERT_EQUAL_HEX32(WDOG_KEY, M.log[0].val);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MODEL_LOCK, M.log[M.n - 1u].off);
    TEST_ASSERT_NOT_EQUAL(WDOG_KEY, M.log[M.n - 1u].val);
    TEST_ASSERT_TRUE(M.locked);
    TEST_ASSERT_EQUAL_UINT(0u, M.ignored);
    TEST_ASSERT_EQUAL_UINT(0u, M.unmodelled);
}

static void test_early_disarms_a_running_watchdog_through_the_lock(void)
{
    preset_running();
    tfw_mps2_model_soc_early();

    TEST_ASSERT_EQUAL_HEX32(0u, M.ctrl);
    TEST_ASSERT_FALSE(M.ris);
    TEST_ASSERT_EQUAL_HEX32(1234u, M.load);
    assert_bracketed();
}

static void test_early_writes_ctrl_zero_then_intclr(void)
{
    preset_running();
    tfw_mps2_model_soc_early();

    TEST_ASSERT_EQUAL_UINT(4u, M.n);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MODEL_CTRL, M.log[1].off);
    TEST_ASSERT_EQUAL_HEX32(0u, M.log[1].val);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MODEL_INTCLR, M.log[2].off);
}

static void test_arm_loads_four_seconds_of_apb_clock(void)
{
    tfw_mps2_model_soc_early();
    tfw_mps2_model_clear_log();

    tfw_mps2_model_watchdog_arm();

    TEST_ASSERT_EQUAL_HEX32(LOAD_4S, M.load);
    TEST_ASSERT_EQUAL_HEX32(LOAD_4S, M.value);
    assert_bracketed();
}

static void test_arm_enables_the_interrupt_and_the_reset(void)
{
    tfw_mps2_model_soc_early();
    tfw_mps2_model_clear_log();

    tfw_mps2_model_watchdog_arm();

    TEST_ASSERT_EQUAL_HEX32(CTRL_INTEN | CTRL_RESEN, M.ctrl);
    TEST_ASSERT_FALSE(M.ris);
    assert_bracketed();
}

static void test_arm_loads_before_it_enables(void)
{
    tfw_mps2_model_watchdog_arm();

    /* the key, LOAD, INTCLR, CTRL, the lock */
    TEST_ASSERT_EQUAL_UINT(5u, M.n);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MODEL_LOAD, M.log[1].off);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MODEL_INTCLR, M.log[2].off);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MODEL_CTRL, M.log[3].off);
}

static void test_arm_marks_the_reset_as_the_watchdog(void)
{
    tfw_mps2_model_watchdog_arm();

    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_mps2_model_reset_mark());
}

static void test_pet_clears_the_interrupt_and_reloads(void)
{
    tfw_mps2_model_watchdog_arm();
    M.value = 5u; /* nearly out, the interrupt raised */
    M.ris   = 1;
    tfw_mps2_model_clear_log();

    tfw_mps2_model_watchdog_pet();

    TEST_ASSERT_FALSE(M.ris);
    TEST_ASSERT_EQUAL_HEX32(LOAD_4S, M.value);
    TEST_ASSERT_EQUAL_HEX32(LOAD_4S, M.load);
    TEST_ASSERT_EQUAL_HEX32(CTRL_INTEN | CTRL_RESEN, M.ctrl);
    TEST_ASSERT_EQUAL_UINT(3u, M.n);
    TEST_ASSERT_EQUAL_HEX32(TFW_MPS2_MODEL_INTCLR, M.log[1].off);
    assert_bracketed();
}

static void test_reset_cause_is_the_marker_decoded(void)
{
    tfw_mps2_model_reset_cause(TFW_CM_MARK_WATCHDOG);

    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_mps2_model_decoded_mark());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_disarms_a_running_watchdog_through_the_lock);
    RUN_TEST(test_early_writes_ctrl_zero_then_intclr);
    RUN_TEST(test_arm_loads_four_seconds_of_apb_clock);
    RUN_TEST(test_arm_enables_the_interrupt_and_the_reset);
    RUN_TEST(test_arm_loads_before_it_enables);
    RUN_TEST(test_arm_marks_the_reset_as_the_watchdog);
    RUN_TEST(test_pet_clears_the_interrupt_and_reloads);
    RUN_TEST(test_reset_cause_is_the_marker_decoded);
    return UNITY_END();
}
