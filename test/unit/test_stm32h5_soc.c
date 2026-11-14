/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 SoC hooks' register values (src/hal/cortex-m/stm32h5/hal_soc.c)
 * through tfw_cm_soc_early, tfw_cm_soc_watchdog_arm and
 * tfw_cm_soc_watchdog_pet on the register file
 * (test/model/cortex-m/regfile_model.c), against RM0481: FLASH_ACR LATENCY 3
 * for HCLK 64 MHz at VOS3 (the FLASH wait-state table), RCC_CR HSIDIV 0 for
 * the undivided HSI, the IWDG start, access and reload keys in that order,
 * PR 3 (LSI/32) and RLR 3999 for a 4 s timeout, and the reload key alone on a
 * pet. A board has not run them.
 *
 * The link wraps tfw_cm_model_reg (-Wl,--wrap): each access to IWDG_KR gets
 * a fresh slot, so the suite reads every key written in order, and every
 * access's address is logged. Every other register is the register file's.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "regfile_model.h"

/* RM0481 register addresses, for the register file's peek. */
#define RCC_CR_ADDR     ((uintptr_t)TFW_H5_RCC_BASE + 0x00u)
#define FLASH_ACR_ADDR  0x40022000u
#define IWDG_KR_ADDR    0x40003000u
#define IWDG_PR_ADDR    0x40003004u
#define IWDG_RLR_ADDR   0x40003008u
#define IWDG_SR_ADDR    0x4000300Cu

/* RM0481 reset values: FLASH_ACR 0x0000_0013 (LATENCY 3, WRHIGHFREQ 1),
 * RCC_CR 0x0000_002B (HSION, HSIRDY, HSIDIV 01 = HSI/2, HSIDIVF). */
#define FLASH_ACR_RESET 0x00000013u
#define RCC_CR_RESET    0x0000002Bu

#define LOG_MAX 16u

/* The suite reaches the SoC hooks by names the link rule (mk/host.mk) binds
 * with --defsym, so no architecture name is spelt outside its directories
 * (lint-arch-leak): the watchdog through the portable tfw_hal_watchdog_arm
 * and tfw_hal_watchdog_pet, as hal_sys.c forwards them, the early hook and
 * the reset marker through h5_soc_early and h5_reset_mark. */
void h5_soc_early(void);
volatile uint32_t h5_reset_mark;

volatile uint32_t *__real_tfw_cm_model_reg(uintptr_t addr);
volatile uint32_t *__wrap_tfw_cm_model_reg(uintptr_t addr);

static uint32_t  kr_keys[LOG_MAX];
static unsigned  kr_n;
static uint32_t  access_log[LOG_MAX];
static unsigned  access_n;

volatile uint32_t *__wrap_tfw_cm_model_reg(uintptr_t addr)
{
    TEST_ASSERT_TRUE_MESSAGE(access_n < LOG_MAX, "access log full");
    access_log[access_n++] = (uint32_t)addr;
    if (addr == IWDG_KR_ADDR) {
        TEST_ASSERT_TRUE_MESSAGE(kr_n < LOG_MAX, "key log full");
        return (volatile uint32_t *)&kr_keys[kr_n++];
    }
    return __real_tfw_cm_model_reg(addr);
}

/* A register's value before the call, set without logging an access. */
static void preset(uintptr_t addr, uint32_t val)
{
    *__real_tfw_cm_model_reg(addr) = val;
}

void setUp(void)
{
    regfile_model_reset();
    kr_n              = 0u;
    access_n          = 0u;
    h5_reset_mark = 0u;
    preset(FLASH_ACR_ADDR, FLASH_ACR_RESET);
    preset(RCC_CR_ADDR, RCC_CR_RESET);
}

void tearDown(void)
{
}

/* RM0481 FLASH wait states, VOS3: 0 WS to 20 MHz, then one per 20 MHz, so
 * 64 MHz needs LATENCY 3; the other ACR bits are left as they were. */
static void test_early_sets_flash_latency_3_for_64mhz(void)
{
    preset(FLASH_ACR_ADDR, 0x30u);
    h5_soc_early();
    TEST_ASSERT_EQUAL_HEX32(0x33u, regfile_model_peek(FLASH_ACR_ADDR));
}

/* RCC_CR HSIDIV (bits 4:3) to 00: HSI/1, 64 MHz; the rest of RCC_CR, the
 * HSIDIVF the model presets among it, is left alone. */
static void test_early_clears_hsidiv_only(void)
{
    h5_soc_early();
    TEST_ASSERT_EQUAL_HEX32(RCC_CR_RESET & ~(0x3u << 3), regfile_model_peek(RCC_CR_ADDR));
}

/* Early reloads a watchdog a previous image may have left running, with the
 * reload key and no other. */
static void test_early_writes_the_reload_key_once(void)
{
    h5_soc_early();
    TEST_ASSERT_EQUAL_UINT(1u, kr_n);
    TEST_ASSERT_EQUAL_HEX32(0xAAAAu, kr_keys[0]);
}

/* RM0481 IWDG: 0xCCCC starts it, 0x5555 opens PR and RLR, 0xAAAA reloads. */
static void test_arm_writes_start_access_reload_keys_in_order(void)
{
    tfw_hal_watchdog_arm();
    TEST_ASSERT_EQUAL_UINT(3u, kr_n);
    TEST_ASSERT_EQUAL_HEX32(0xCCCCu, kr_keys[0]);
    TEST_ASSERT_EQUAL_HEX32(0x5555u, kr_keys[1]);
    TEST_ASSERT_EQUAL_HEX32(0xAAAAu, kr_keys[2]);
}

/* PR and RLR are written after the access key, and SR is read for the
 * update to finish before the reload. */
static void test_arm_writes_pr_and_rlr_then_waits_on_sr(void)
{
    static const uint32_t want[] = {
        IWDG_KR_ADDR, IWDG_KR_ADDR, IWDG_PR_ADDR, IWDG_RLR_ADDR, IWDG_SR_ADDR, IWDG_KR_ADDR,
    };

    tfw_hal_watchdog_arm();
    TEST_ASSERT_EQUAL_UINT(sizeof want / sizeof want[0], access_n);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(want, access_log, sizeof want / sizeof want[0]);
}

/* LSI 32 kHz, 4 s: PR 3 (divider 32), RLR 3999 (4000 counts). */
static void test_arm_sets_pr_3_and_rlr_3999(void)
{
    tfw_hal_watchdog_arm();
    TEST_ASSERT_EQUAL_HEX32(3u, regfile_model_peek(IWDG_PR_ADDR));
    TEST_ASSERT_EQUAL_UINT32(3999u, regfile_model_peek(IWDG_RLR_ADDR));
}

/* RM0481 IWDG timeout: (4 << PR) * (RLR + 1) / LSI, here 4 s. */
static void test_arm_timeout_is_4_seconds(void)
{
    uint32_t pr;
    uint32_t rl;

    tfw_hal_watchdog_arm();
    pr = regfile_model_peek(IWDG_PR_ADDR);
    rl = regfile_model_peek(IWDG_RLR_ADDR);
    TEST_ASSERT_EQUAL_UINT32(4u * 32000u, (4u << pr) * (rl + 1u));
}

/* The arm marks the reset cause before the watchdog can bite. */
static void test_arm_marks_the_watchdog(void)
{
    tfw_hal_watchdog_arm();
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, h5_reset_mark);
}

/* A pet is the reload key alone, touching no other register. */
static void test_pet_writes_the_reload_key_only(void)
{
    tfw_hal_watchdog_pet();
    TEST_ASSERT_EQUAL_UINT(1u, kr_n);
    TEST_ASSERT_EQUAL_HEX32(0xAAAAu, kr_keys[0]);
    TEST_ASSERT_EQUAL_UINT(1u, access_n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_sets_flash_latency_3_for_64mhz);
    RUN_TEST(test_early_clears_hsidiv_only);
    RUN_TEST(test_early_writes_the_reload_key_once);
    RUN_TEST(test_arm_writes_start_access_reload_keys_in_order);
    RUN_TEST(test_arm_writes_pr_and_rlr_then_waits_on_sr);
    RUN_TEST(test_arm_sets_pr_3_and_rlr_3999);
    RUN_TEST(test_arm_timeout_is_4_seconds);
    RUN_TEST(test_arm_marks_the_watchdog);
    RUN_TEST(test_pet_writes_the_reload_key_only);
    return UNITY_END();
}
