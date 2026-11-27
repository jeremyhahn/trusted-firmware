/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 SoC bring-up (src/hal/cortex-m/stm32h5/hal_soc.c) run over a
 * register model: the clock raise, the watchdog and the reset cause. Every
 * address and bit is soc_regs.h's. boot/hal/cortex-m/rp2350/test/cm_regs.h
 * comes ahead of the real one on the include path, so every TFW_CM_REG lands
 * in tfw_cm_model_reg below.
 *
 * The model cannot tell a read from a write: each access is one call. So at
 * every call it compares each register with the value it last left there,
 * and a difference is a write, logged in order with the poll counts at that
 * moment. A write the code makes last is logged by model_flush. A store of
 * the value already there is no difference, so the model also counts every
 * call per register: a register the code must leave alone has none. Three
 * registers act as the part does:
 *   FLASH_ACR  a new LATENCY reads back only on the Nth read after the write
 *   RCC_CR     HSIDIVF sets on the Nth read after HSIDIV is written
 *   IWDG_SR    PR and RLR writes set the busy bits, clear on the Nth read
 *   IWDG_KR    write-only: it reads 0 after each write
 */
#include <stdint.h>
#include "unity.h"
#include "cm_regs.h"
#include "tfw/config.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "h5_calc.h"
#include "soc_reset.h"

#define ADDR_RCC_CR   (TFW_H5_RCC_BASE + 0x00u)
#define ADDR_RCC_RSR  (TFW_H5_RCC_BASE + 0xF4u)
#define ADDR_ACR      0x40022000u
#define ADDR_KR       0x40003000u
#define ADDR_PR       0x40003004u
#define ADDR_RLR      0x40003008u
#define ADDR_SR       0x4000300Cu

/* The reads each poll takes before its bit shows. */
#define ACR_POLLS     3u
#define HSIDIVF_POLLS 4u
#define SR_POLLS      5u

/* Reset values with bits the code must leave alone: ACR LATENCY 5,
 * WRHIGHFREQ 3, PRFTEN; RCC_CR HSION, HSIRDY, HSIDIV 1 (HSI / 2), HSI48ON
 * and two high bits, HSIDIVF clear. */
#define ACR_INIT      0x00000135u
#define RCC_CR_INIT   (0x50000003u | (0x1u << 3) | TFW_H5_RCC_CR_HSI48ON)

/* An inherited watchdog configuration: PR /256, RLR half the counter. */
#define INHERITED_PR  6u
#define INHERITED_RLR 0x7FFu

volatile uint32_t tfw_cm_reset_mark;

#define MODEL_REGS 16u
#define MODEL_LOG  16u

struct model_write {
    uint32_t addr;
    uint32_t val;
    uint32_t acr_reads;      /* ACR reads since its LATENCY write */
    uint32_t sr_reads;       /* SR reads since the busy bits set */
    uint32_t mark;           /* tfw_cm_reset_mark when it was seen */
};

static uint32_t          model_addr[MODEL_REGS];
static volatile uint32_t model_val[MODEL_REGS];
static uint32_t          model_seen[MODEL_REGS];
static uint32_t          model_access[MODEL_REGS];   /* calls, read or write */
static uint32_t          model_used;

static struct model_write model_log[MODEL_LOG];
static uint32_t           model_logged;

static uint32_t acr_pending;     /* the LATENCY not yet shown */
static uint32_t acr_reads;
static uint8_t  acr_waiting;
static uint32_t rcc_reads;
static uint8_t  rcc_waiting;
static uint32_t sr_reads;
static uint8_t  sr_waiting;
static uint32_t sr_late_bits;    /* busy bits still set at read SR_POLLS */
static uint32_t sr_late_polls;   /* the further reads they take to clear */

static void model_set(uint32_t addr, uint32_t val);

static void model_wrote(uint32_t i)
{
    uint32_t addr = model_addr[i];
    uint32_t val  = model_val[i];

    if (model_logged == MODEL_LOG) {
        TEST_FAIL_MESSAGE("write log full");
    }
    model_log[model_logged].addr      = addr;
    model_log[model_logged].val       = val;
    model_log[model_logged].acr_reads = acr_reads;
    model_log[model_logged].sr_reads  = sr_reads;
    model_log[model_logged].mark      = tfw_cm_reset_mark;
    model_logged++;

    if (addr == ADDR_ACR) {
        /* The new LATENCY is the flash's only after a few reads. */
        acr_pending  = val;
        model_val[i] = (val & ~TFW_H5_FLASH_ACR_LATENCY) |
                       (model_seen[i] & TFW_H5_FLASH_ACR_LATENCY);
        acr_reads    = 0u;
        acr_waiting  = 1u;
    } else if (addr == ADDR_RCC_CR) {
        rcc_reads   = 0u;
        rcc_waiting = 1u;
    } else if (addr == ADDR_PR || addr == ADDR_RLR) {
        model_set(ADDR_SR, TFW_H5_IWDG_SR_BUSY);
        sr_reads   = 0u;
        sr_waiting = 1u;
    } else if (addr == ADDR_KR) {
        model_val[i] = 0u;
    }
    model_seen[i] = model_val[i];
}

/* Logs every register that changed since the model last looked. */
static void model_flush(void)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_val[i] != model_seen[i]) {
            model_wrote(i);
        }
    }
}

static volatile uint32_t *model_slot(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return &model_val[i];
        }
    }
    if (model_used == MODEL_REGS) {
        TEST_FAIL_MESSAGE("register model full");
    }
    model_addr[model_used] = addr;
    model_val[model_used]  = 0u;
    model_seen[model_used] = 0u;
    model_access[model_used] = 0u;
    return &model_val[model_used++];
}

/* Sets a register without logging a write: the part's own state. */
static void model_set(uint32_t addr, uint32_t val)
{
    volatile uint32_t *r = model_slot(addr);
    uint32_t i = (uint32_t)(r - model_val);

    *r = val;
    model_seen[i] = val;
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    volatile uint32_t *r;

    model_flush();
    r = model_slot(addr);
    model_access[r - model_val]++;
    if (addr == ADDR_ACR && acr_waiting) {
        if (++acr_reads == ACR_POLLS) {
            model_set(addr, acr_pending);
            acr_waiting = 0u;
        }
    } else if (addr == ADDR_RCC_CR && rcc_waiting) {
        if (++rcc_reads == HSIDIVF_POLLS) {
            model_set(addr, *r | TFW_H5_RCC_CR_HSIDIVF);
            rcc_waiting = 0u;
        }
    } else if (addr == ADDR_SR && sr_waiting) {
        ++sr_reads;
        if (sr_reads == SR_POLLS) {
            /* PVU and RVU clear apart: sr_late_bits lag the rest. */
            model_set(addr, sr_late_bits);
            if (sr_late_bits == 0u) {
                sr_waiting = 0u;
            }
        } else if (sr_reads == SR_POLLS + sr_late_polls) {
            model_set(addr, 0u);
            sr_waiting = 0u;
        }
    }
    return r;
}

/* The register's value now, without counting a read. */
static uint32_t reg(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return model_val[i];
        }
    }
    return 0u;
}

/* The index in the log of the nth write to addr, or MODEL_LOG. */
static uint32_t nth_write(uint32_t addr, uint32_t n)
{
    uint32_t i;

    for (i = 0u; i < model_logged; i++) {
        if (model_log[i].addr == addr && n-- == 0u) {
            return i;
        }
    }
    return MODEL_LOG;
}

static uint32_t writes_to(uint32_t addr)
{
    uint32_t i;
    uint32_t n = 0u;

    for (i = 0u; i < model_logged; i++) {
        if (model_log[i].addr == addr) {
            n++;
        }
    }
    return n;
}

/* The code's calls on addr, reads and writes: a store of the value already
 * there is no difference to log, but it is still a call. */
static uint32_t accesses(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return model_access[i];
        }
    }
    return 0u;
}

/* A watchdog an earlier stage armed: PR and RLR not at zero, so a zero
 * written over them is a difference the model logs. */
static void inherit_armed_watchdog(void)
{
    model_set(ADDR_PR, INHERITED_PR);
    model_set(ADDR_RLR, INHERITED_RLR);
}

/* tfw_h5_reset_take's stand-in: soc_reset.c has its own test. */
static volatile uint32_t *take_rsr;
static uint8_t            take_requested;
static uint32_t           take_calls;
static tfw_reset_cause_t  take_result;

tfw_reset_cause_t tfw_h5_reset_take(volatile uint32_t *rsr, uint8_t requested)
{
    take_rsr       = rsr;
    take_requested = requested;
    take_calls++;
    return take_result;
}

void setUp(void)
{
    model_used     = 0u;
    model_logged   = 0u;
    acr_waiting    = 0u;
    rcc_waiting    = 0u;
    sr_waiting     = 0u;
    acr_reads      = 0u;
    rcc_reads      = 0u;
    sr_reads       = 0u;
    sr_late_bits   = 0u;
    sr_late_polls  = 0u;
    take_rsr       = 0;
    take_requested = 0xFFu;
    take_calls     = 0u;
    take_result    = TFW_RESET_WATCHDOG;
    tfw_cm_reset_mark = 0u;
    model_set(ADDR_ACR, ACR_INIT);
    model_set(ADDR_RCC_CR, RCC_CR_INIT);
}

void tearDown(void) {}

/* ---- tfw_cm_soc_early ---- */

static void test_early_pets_the_watchdog_first(void)
{
    inherit_armed_watchdog();
    tfw_cm_soc_early();
    model_flush();

    TEST_ASSERT_TRUE(model_logged > 0u);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[0].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_RELOAD, model_log[0].val);
    /* A pet only: no other key, no prescaler or reload. */
    TEST_ASSERT_EQUAL_UINT32(1u, writes_to(ADDR_KR));
    TEST_ASSERT_EQUAL_UINT32(0u, writes_to(ADDR_PR));
    TEST_ASSERT_EQUAL_UINT32(0u, writes_to(ADDR_RLR));
    /* Not even a store of the value already there. */
    TEST_ASSERT_EQUAL_UINT32(0u, accesses(ADDR_PR));
    TEST_ASSERT_EQUAL_UINT32(0u, accesses(ADDR_RLR));
    TEST_ASSERT_EQUAL_HEX32(INHERITED_PR, reg(ADDR_PR));
    TEST_ASSERT_EQUAL_HEX32(INHERITED_RLR, reg(ADDR_RLR));
}

static void test_early_writes_the_wait_states_before_the_divider(void)
{
    uint8_t  ws = tfw_h5_flash_ws_vos3((uint32_t)TFW_CM_CPU_HZ);
    uint32_t acr;
    uint32_t cr;

    TEST_ASSERT_EQUAL_UINT8(3u, ws);   /* 64 MHz at VOS3: 60 < f <= 80 */

    tfw_cm_soc_early();
    model_flush();

    acr = nth_write(ADDR_ACR, 0u);
    cr  = nth_write(ADDR_RCC_CR, 0u);
    TEST_ASSERT_TRUE(acr < MODEL_LOG);
    TEST_ASSERT_TRUE(cr < MODEL_LOG);
    TEST_ASSERT_EQUAL_UINT32(1u, writes_to(ADDR_ACR));
    TEST_ASSERT_EQUAL_UINT32(1u, writes_to(ADDR_RCC_CR));
    TEST_ASSERT_EQUAL_HEX32(ws, model_log[acr].val & TFW_H5_FLASH_ACR_LATENCY);
    TEST_ASSERT_TRUE(acr < cr);
    /* The divider is cleared only once LATENCY reads back. */
    TEST_ASSERT_EQUAL_UINT32(ACR_POLLS, model_log[cr].acr_reads);
    TEST_ASSERT_EQUAL_HEX32(0u, model_log[cr].val & TFW_H5_RCC_CR_HSIDIV);
}

static void test_early_polls_latency_and_hsidivf_until_they_show(void)
{
    tfw_cm_soc_early();
    model_flush();

    TEST_ASSERT_EQUAL_UINT32(ACR_POLLS, acr_reads);
    TEST_ASSERT_EQUAL_UINT8(0u, acr_waiting);
    TEST_ASSERT_EQUAL_UINT32(HSIDIVF_POLLS, rcc_reads);
    TEST_ASSERT_EQUAL_UINT8(0u, rcc_waiting);
}

static void test_early_keeps_the_other_acr_and_rcc_cr_bits(void)
{
    uint8_t ws = tfw_h5_flash_ws_vos3((uint32_t)TFW_CM_CPU_HZ);

    tfw_cm_soc_early();
    model_flush();

    TEST_ASSERT_EQUAL_HEX32((ACR_INIT & ~TFW_H5_FLASH_ACR_LATENCY) | ws, reg(ADDR_ACR));
    TEST_ASSERT_EQUAL_HEX32((RCC_CR_INIT & ~TFW_H5_RCC_CR_HSIDIV) | TFW_H5_RCC_CR_HSIDIVF,
                            reg(ADDR_RCC_CR));
}

/* HSIDIV is two bits: an HSI / 8 start (both set) must end at HSI / 1. */
static void test_early_clears_both_hsidiv_bits_from_hsi_over_8(void)
{
    uint32_t cr;

    model_set(ADDR_RCC_CR, (RCC_CR_INIT & ~TFW_H5_RCC_CR_HSIDIV) | (0x3u << 3));

    tfw_cm_soc_early();
    model_flush();

    cr = nth_write(ADDR_RCC_CR, 0u);
    TEST_ASSERT_TRUE(cr < MODEL_LOG);
    TEST_ASSERT_EQUAL_UINT32(1u, writes_to(ADDR_RCC_CR));
    TEST_ASSERT_EQUAL_HEX32(0x50000003u | TFW_H5_RCC_CR_HSI48ON, model_log[cr].val);
    TEST_ASSERT_EQUAL_HEX32(0x50000003u | TFW_H5_RCC_CR_HSI48ON | TFW_H5_RCC_CR_HSIDIVF,
                            reg(ADDR_RCC_CR));
}

/* ---- tfw_cm_soc_watchdog_arm / _pet ---- */

static void test_arm_marks_then_keys_enable_access_pr_rlr_wait_reload(void)
{
    uint8_t  pr = 0xFFu;
    uint16_t rl = 0xFFFFu;

    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_iwdg_timing(32000uL, 4uL, &pr, &rl));

    tfw_cm_soc_watchdog_arm();
    model_flush();

    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_cm_reset_mark);
    TEST_ASSERT_EQUAL_UINT32(5u, model_logged);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[0].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_ENABLE, model_log[0].val);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[1].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_ACCESS, model_log[1].val);
    TEST_ASSERT_EQUAL_HEX32(ADDR_PR, model_log[2].addr);
    TEST_ASSERT_EQUAL_HEX32(pr, model_log[2].val);
    TEST_ASSERT_EQUAL_HEX32(ADDR_RLR, model_log[3].addr);
    TEST_ASSERT_EQUAL_HEX32(rl, model_log[3].val);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[4].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_RELOAD, model_log[4].val);
    /* RELOAD only once SR's busy bits clear, on the poll's last read. */
    TEST_ASSERT_EQUAL_UINT32(SR_POLLS, model_log[4].sr_reads);
    TEST_ASSERT_EQUAL_UINT32(SR_POLLS, sr_reads);
}

static void test_arm_times_four_seconds_of_the_32_khz_lsi(void)
{
    tfw_cm_soc_watchdog_arm();
    model_flush();

    /* 32000 * 4 / (4 << PR) - 1 <= 0xFFF first at PR 3 (/32): 3999. */
    TEST_ASSERT_EQUAL_HEX32(3u, reg(ADDR_PR));
    TEST_ASSERT_EQUAL_HEX32(3999u, reg(ADDR_RLR));
}

static void test_arm_reloads_only_once_rvu_clears_after_pvu(void)
{
    sr_late_bits  = 0x2u;   /* RVU */
    sr_late_polls = 2u;

    tfw_cm_soc_watchdog_arm();
    model_flush();

    TEST_ASSERT_EQUAL_UINT32(5u, model_logged);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[4].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_RELOAD, model_log[4].val);
    TEST_ASSERT_EQUAL_UINT32(SR_POLLS + 2u, model_log[4].sr_reads);
    TEST_ASSERT_EQUAL_HEX32(0u, reg(ADDR_SR));
}

static void test_arm_reloads_only_once_pvu_clears_after_rvu(void)
{
    sr_late_bits  = 0x1u;   /* PVU */
    sr_late_polls = 3u;

    tfw_cm_soc_watchdog_arm();
    model_flush();

    TEST_ASSERT_EQUAL_UINT32(5u, model_logged);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[4].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_RELOAD, model_log[4].val);
    TEST_ASSERT_EQUAL_UINT32(SR_POLLS + 3u, model_log[4].sr_reads);
    TEST_ASSERT_EQUAL_HEX32(0u, reg(ADDR_SR));
}

static void test_arm_reloads_only_once_wvu_clears_after_pvu_and_rvu(void)
{
    sr_late_bits  = 0x4u;   /* WVU */
    sr_late_polls = 2u;

    tfw_cm_soc_watchdog_arm();
    model_flush();

    TEST_ASSERT_EQUAL_UINT32(5u, model_logged);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[4].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_RELOAD, model_log[4].val);
    TEST_ASSERT_EQUAL_UINT32(SR_POLLS + 2u, model_log[4].sr_reads);
    TEST_ASSERT_EQUAL_HEX32(0u, reg(ADDR_SR));
}

static void test_arm_reloads_only_once_ewu_clears_after_the_rest(void)
{
    sr_late_bits  = 0x8u;   /* EWU */
    sr_late_polls = 3u;

    tfw_cm_soc_watchdog_arm();
    model_flush();

    TEST_ASSERT_EQUAL_UINT32(5u, model_logged);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[4].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_RELOAD, model_log[4].val);
    TEST_ASSERT_EQUAL_UINT32(SR_POLLS + 3u, model_log[4].sr_reads);
    TEST_ASSERT_EQUAL_HEX32(0u, reg(ADDR_SR));
}

/* The mark is in place before the watchdog can run, so a reset the new
 * watchdog causes is read as one. */
static void test_arm_sets_the_watchdog_mark_before_its_first_key(void)
{
    tfw_cm_soc_watchdog_arm();
    model_flush();

    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[0].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_ENABLE, model_log[0].val);
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, model_log[0].mark);
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, model_log[4].mark);
}

/* An earlier stage's watchdog and mark: the arm stores its own PR, RLR
 * and mark over them, not ORed into what was there. */
static void test_arm_over_an_inherited_watchdog_and_mark_writes_its_own(void)
{
    inherit_armed_watchdog();
    tfw_cm_reset_mark = TFW_CM_MARK_PANIC;

    tfw_cm_soc_watchdog_arm();
    model_flush();

    TEST_ASSERT_EQUAL_UINT32(5u, model_logged);
    TEST_ASSERT_EQUAL_HEX32(ADDR_PR, model_log[2].addr);
    TEST_ASSERT_EQUAL_HEX32(3u, model_log[2].val);
    TEST_ASSERT_EQUAL_HEX32(ADDR_RLR, model_log[3].addr);
    TEST_ASSERT_EQUAL_HEX32(3999u, model_log[3].val);
    TEST_ASSERT_EQUAL_HEX32(3u, reg(ADDR_PR));
    TEST_ASSERT_EQUAL_HEX32(3999u, reg(ADDR_RLR));
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, model_log[0].mark);
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_cm_reset_mark);
}

static void test_pet_writes_reload_only(void)
{
    inherit_armed_watchdog();
    tfw_cm_soc_watchdog_pet();
    model_flush();

    TEST_ASSERT_EQUAL_UINT32(1u, model_logged);
    TEST_ASSERT_EQUAL_HEX32(ADDR_KR, model_log[0].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_IWDG_KEY_RELOAD, model_log[0].val);
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_cm_reset_mark);
    TEST_ASSERT_EQUAL_UINT32(0u, accesses(ADDR_PR));
    TEST_ASSERT_EQUAL_UINT32(0u, accesses(ADDR_RLR));
    TEST_ASSERT_EQUAL_HEX32(INHERITED_PR, reg(ADDR_PR));
    TEST_ASSERT_EQUAL_HEX32(INHERITED_RLR, reg(ADDR_RLR));
}

/* ---- tfw_cm_soc_reset_cause ---- */

static void test_reset_cause_hands_rcc_rsr_and_resetreq_to_reset_take(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_cm_soc_reset_cause(TFW_CM_MARK_RESETREQ));
    TEST_ASSERT_EQUAL_UINT32(1u, take_calls);
    TEST_ASSERT_EQUAL_PTR(tfw_cm_model_reg(ADDR_RCC_RSR), take_rsr);
    TEST_ASSERT_EQUAL_UINT8(1u, take_requested);
}

static void test_reset_cause_any_other_mark_is_not_requested(void)
{
    (void)tfw_cm_soc_reset_cause(TFW_CM_MARK_WATCHDOG);
    TEST_ASSERT_EQUAL_UINT8(0u, take_requested);
    (void)tfw_cm_soc_reset_cause(TFW_CM_MARK_PANIC);
    TEST_ASSERT_EQUAL_UINT8(0u, take_requested);
    (void)tfw_cm_soc_reset_cause(0u);
    TEST_ASSERT_EQUAL_UINT8(0u, take_requested);
    TEST_ASSERT_EQUAL_UINT32(3u, take_calls);
    TEST_ASSERT_EQUAL_PTR(tfw_cm_model_reg(ADDR_RCC_RSR), take_rsr);
}

/* The cause is reset_take's decode, whatever it is, not a fixed one. */
static void test_reset_cause_returns_what_reset_take_decodes(void)
{
    take_result = TFW_RESET_BROWNOUT;
    TEST_ASSERT_EQUAL(TFW_RESET_BROWNOUT, tfw_cm_soc_reset_cause(TFW_CM_MARK_WATCHDOG));
    take_result = TFW_RESET_SOFTWARE;
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_cm_soc_reset_cause(TFW_CM_MARK_RESETREQ));
    TEST_ASSERT_EQUAL_UINT8(1u, take_requested);
    TEST_ASSERT_EQUAL_UINT32(2u, take_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_pets_the_watchdog_first);
    RUN_TEST(test_early_writes_the_wait_states_before_the_divider);
    RUN_TEST(test_early_polls_latency_and_hsidivf_until_they_show);
    RUN_TEST(test_early_keeps_the_other_acr_and_rcc_cr_bits);
    RUN_TEST(test_early_clears_both_hsidiv_bits_from_hsi_over_8);
    RUN_TEST(test_arm_marks_then_keys_enable_access_pr_rlr_wait_reload);
    RUN_TEST(test_arm_times_four_seconds_of_the_32_khz_lsi);
    RUN_TEST(test_arm_reloads_only_once_rvu_clears_after_pvu);
    RUN_TEST(test_arm_reloads_only_once_pvu_clears_after_rvu);
    RUN_TEST(test_arm_reloads_only_once_wvu_clears_after_pvu_and_rvu);
    RUN_TEST(test_arm_reloads_only_once_ewu_clears_after_the_rest);
    RUN_TEST(test_arm_sets_the_watchdog_mark_before_its_first_key);
    RUN_TEST(test_arm_over_an_inherited_watchdog_and_mark_writes_its_own);
    RUN_TEST(test_pet_writes_reload_only);
    RUN_TEST(test_reset_cause_hands_rcc_rsr_and_resetreq_to_reset_take);
    RUN_TEST(test_reset_cause_any_other_mark_is_not_requested);
    RUN_TEST(test_reset_cause_returns_what_reset_take_decodes);
    return UNITY_END();
}
