/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mps2 SoC's watchdog (hal_soc.c) over a register model, once under each
 * MPS2 profile (mk/boot.mk): boot/hal/cortex-m/rp2350/test/cm_regs.h comes
 * ahead of src/hal/cortex-m, so every TFW_CM_REG lands in tfw_cm_model_reg
 * below. The model logs each access in order, the value the register holds
 * after it, so a test reads both what was written and when: a write outside
 * the unlock is one the hardware ignores.
 *
 * TEST_WDOG_BASE is the profile's watchdog by literal (the AN505's Secure
 * one, the AN500's APB one), set by the make rule, not read from the
 * profile, so a moved base fails here.
 */
#include <stdint.h>
#include "unity.h"
#include "cm_regs.h"
#include "hal_early.h"
#include "soc_regs.h"

#ifndef TEST_WDOG_BASE
#error "TEST_WDOG_BASE: the make rule states the profile's watchdog base"
#endif

#define LOG_MAX   32u
#define MODEL_MAX 16u

#define WDOG_LOAD   (TEST_WDOG_BASE + 0x000u)
#define WDOG_CTRL   (TEST_WDOG_BASE + 0x008u)
#define WDOG_INTCLR (TEST_WDOG_BASE + 0x00Cu)
#define WDOG_LOCK   (TEST_WDOG_BASE + 0xC00u)

/* What hal_early.c defines on the part: the marker word and its decode. */
volatile uint32_t tfw_cm_reset_mark;

tfw_reset_cause_t tfw_cm_mark_decode(uint32_t mark)
{
    return tfw_cm_reset_decode(mark);
}

/* Each access in order: the register, and its value after the access. */
static uint32_t log_addr[LOG_MAX];
static uint32_t log_val[LOG_MAX];
static uint32_t log_used;

/* The registers' values between accesses. */
static uint32_t model_addr[MODEL_MAX];
static uint32_t model_val[MODEL_MAX];
static uint32_t model_used;

static uint32_t *model_slot(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return &model_val[i];
        }
    }
    if (model_used == MODEL_MAX) {
        TEST_FAIL_MESSAGE("register model full");
    }
    model_addr[model_used] = addr;
    model_val[model_used] = 0u;
    return &model_val[model_used++];
}

/* The last access is done once the next one starts: keep what it left. */
static void log_settle(void)
{
    if (log_used > 0u) {
        *model_slot(log_addr[log_used - 1u]) = log_val[log_used - 1u];
    }
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    log_settle();
    if (log_used == LOG_MAX) {
        TEST_FAIL_MESSAGE("access log full");
    }
    log_addr[log_used] = addr;
    log_val[log_used] = *model_slot(addr);
    return &log_val[log_used++];
}

static uint32_t accesses(void)
{
    log_settle();
    return log_used;
}

static void expect_access(uint32_t i, uint32_t addr, uint32_t val)
{
    TEST_ASSERT_LESS_THAN_UINT32(accesses(), i);
    TEST_ASSERT_EQUAL_HEX32(addr, log_addr[i]);
    TEST_ASSERT_EQUAL_HEX32(val, log_val[i]);
}

void setUp(void)
{
    log_used = 0u;
    model_used = 0u;
    tfw_cm_reset_mark = 0u;
}

void tearDown(void) {}

static void test_early_disarms_inside_the_unlock(void)
{
    tfw_cm_soc_early();
    TEST_ASSERT_EQUAL_UINT32(4u, accesses());
    expect_access(0u, WDOG_LOCK, TFW_MPS2_WDOG_UNLOCK);
    expect_access(1u, WDOG_CTRL, 0u);
    expect_access(2u, WDOG_INTCLR, 1u);
    expect_access(3u, WDOG_LOCK, 0u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_disarms_inside_the_unlock);
    return UNITY_END();
}
