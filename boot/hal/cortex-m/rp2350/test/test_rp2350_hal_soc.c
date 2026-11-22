/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350's clock bring-up, watchdog and reset cause
 * (src/hal/cortex-m/rp2350/hal_soc.c) run over a register model, every address
 * a literal from [RP-DS] release 8: XOSC at 0x40048000 §8.2.8, CLOCKS at
 * 0x40010000 §8.1.7, TICKS at 0x40108000 §8.5.2, RESETS at 0x40020000 §7.5.3,
 * WATCHDOG at 0x400D8000 §12.9.6, PSM WDSEL at 0x40018008 §7.4.4, POWMAN
 * CHIP_RESET at 0x4010002C §6.4, the CLR alias at +0x3000 §2.1.3.
 *
 * The model logs every access by address, and a status register reads its
 * ready value only from its Nth access on: the bring-up's polls are counted,
 * not timed.
 */
#include <stdint.h>
#include "unity.h"
#include "cm_regs.h"
#include "hal_early.h"
#include "soc_reset.h"

#define XOSC_CTRL       0x40048000u
#define XOSC_STATUS     0x40048004u
#define XOSC_STARTUP    0x4004800Cu
#define CLK_REF_CTRL    0x40010030u
#define CLK_REF_DIV     0x40010034u
#define CLK_REF_SEL     0x40010038u
#define CLK_SYS_CTRL    0x4001003Cu
#define CLK_SYS_DIV     0x40010040u
#define CLK_SYS_SEL     0x40010044u
#define CLK_PERI_CTRL   0x40010048u
#define CLK_PERI_DIV    0x4001004Cu
#define TICKS_T0_CTRL   0x40108018u
#define TICKS_T0_CYCLES 0x4010801Cu
#define TICKS_WD_CTRL   0x40108030u
#define TICKS_WD_CYCLES 0x40108034u
#define RESETS_CLR      0x40023000u
#define RESETS_DONE     0x40020008u
#define WDOG_CTRL       0x400D8000u
#define WDOG_LOAD       0x400D8004u
#define WDOG_REASON     0x400D8008u
#define WDOG_CTRL_CLR   0x400DB000u
#define PSM_WDSEL       0x40018008u
#define POWMAN_CHIP_RST 0x4010002Cu

/* IO_BANK0 bit 6, PADS_BANK0 bit 9, TIMER0 bit 23. */
#define RESETS_WANTED   ((1u << 6) | (1u << 9) | (1u << 23))

volatile uint32_t tfw_cm_reset_mark;

/* soc_reset.c's, which test_rp2350_reset covers: here only what it is handed. */
static const volatile uint32_t *take_reason;
static const volatile uint32_t *take_chip_reset;
static uint8_t take_requested;
static uint32_t take_calls;

tfw_reset_cause_t tfw_rp2350_reset_take(const volatile uint32_t *reason,
                                        const volatile uint32_t *chip_reset,
                                        uint8_t requested)
{
    take_reason = reason;
    take_chip_reset = chip_reset;
    take_requested = requested;
    take_calls++;
    return TFW_RESET_EXTERNAL;
}

#define MODEL_REGS  32u
#define MODEL_TRACE 256u
#define MODEL_POLLS 4u

static uint32_t model_addr[MODEL_REGS];
static volatile uint32_t model_val[MODEL_REGS];
static uint32_t model_used;

/* Every access in order, and the value its register held after it (filled in
 * at the next access, or by trace_close). */
static uint32_t trace_addr[MODEL_TRACE];
static uint32_t trace_val[MODEL_TRACE];
static volatile uint32_t *trace_reg[MODEL_TRACE];
static uint32_t trace_used;

/* A status register reads idle until its ready_at-th access. */
static struct {
    uint32_t addr;
    uint32_t idle;
    uint32_t ready;
    uint32_t ready_at;
    uint32_t reads;
} poll[MODEL_POLLS];

static volatile uint32_t *slot(uint32_t addr)
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
    model_val[model_used] = 0u;
    return &model_val[model_used++];
}

static void trace_close(void)
{
    if (trace_used > 0u) {
        trace_val[trace_used - 1u] = *trace_reg[trace_used - 1u];
    }
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    volatile uint32_t *r = slot(addr);
    uint32_t i;

    trace_close();
    if (trace_used == MODEL_TRACE) {
        TEST_FAIL_MESSAGE("register trace full");
    }
    trace_addr[trace_used] = addr;
    trace_reg[trace_used] = r;
    trace_used++;

    for (i = 0u; i < MODEL_POLLS; i++) {
        if (poll[i].addr == addr) {
            poll[i].reads++;
            *r = poll[i].reads >= poll[i].ready_at ? poll[i].ready : poll[i].idle;
        }
    }
    return r;
}

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

static uint32_t accesses(uint32_t addr)
{
    uint32_t i, n = 0u;

    for (i = 0u; i < trace_used; i++) {
        n += trace_addr[i] == addr ? 1u : 0u;
    }
    return n;
}

/* The trace index of the first or last access to addr; MODEL_TRACE if none. */
static uint32_t first(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < trace_used; i++) {
        if (trace_addr[i] == addr) {
            return i;
        }
    }
    return MODEL_TRACE;
}

static uint32_t last(uint32_t addr)
{
    uint32_t i;

    for (i = trace_used; i > 0u; i--) {
        if (trace_addr[i - 1u] == addr) {
            return i - 1u;
        }
    }
    return MODEL_TRACE;
}

static void script(uint32_t i, uint32_t addr, uint32_t idle, uint32_t ready, uint32_t ready_at)
{
    poll[i].addr = addr;
    poll[i].idle = idle;
    poll[i].ready = ready;
    poll[i].ready_at = ready_at;
    poll[i].reads = 0u;
}

/* XOSC stable on its 3rd read, clk_sys on clk_ref on its 2nd, clk_ref on the
 * crystal on its 4th (ROSC selected before), the three blocks out of reset on
 * the 5th (IO_BANK0 alone before). */
static void script_bring_up(void)
{
    script(0u, XOSC_STATUS, 0u, 0x80000000u, 3u);
    script(1u, CLK_SYS_SEL, 0u, 1u << 0, 2u);
    script(2u, CLK_REF_SEL, 1u << 0, 1u << 2, 4u);
    script(3u, RESETS_DONE, 1u << 6, RESETS_WANTED, 5u);
}

/* A fresh trace, the model's values kept: a second run over the state the
 * first left. Every status register then reads ready at once. */
static void trace_reset(void)
{
    uint32_t i;

    trace_used = 0u;
    for (i = 0u; i < MODEL_POLLS; i++) {
        poll[i].reads = 0u;
        poll[i].ready_at = 1u;
    }
}

void setUp(void)
{
    uint32_t i;

    model_used = 0u;
    trace_used = 0u;
    for (i = 0u; i < MODEL_POLLS; i++) {
        poll[i].addr = 0u;
    }
    tfw_cm_reset_mark = 0u;
    take_reason = 0;
    take_chip_reset = 0;
    take_requested = 0xFFu;
    take_calls = 0u;
}

void tearDown(void) {}

static void test_early_waits_on_each_status_bit_in_order(void)
{
    script_bring_up();
    tfw_cm_soc_early();
    trace_close();

    /* The watchdog goes off first, through the CLR alias. */
    TEST_ASSERT_EQUAL_UINT32(0u, first(WDOG_CTRL_CLR));
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, reg(WDOG_CTRL_CLR));
    /* STARTUP ((12000 + 128) / 256) * 64, CTRL 1-15 MHz | ENABLE 0xFAB. */
    TEST_ASSERT_EQUAL_UINT32(47u * 64u, reg(XOSC_STARTUP));
    TEST_ASSERT_EQUAL_HEX32(0x00FABAA0u, reg(XOSC_CTRL));
    TEST_ASSERT_TRUE(last(XOSC_CTRL) < first(XOSC_STATUS));

    /* Each poll reads until its bit appears and not once more. */
    TEST_ASSERT_EQUAL_UINT32(3u, accesses(XOSC_STATUS));
    TEST_ASSERT_EQUAL_UINT32(2u, accesses(CLK_SYS_SEL));
    TEST_ASSERT_EQUAL_UINT32(4u, accesses(CLK_REF_SEL));
    TEST_ASSERT_EQUAL_UINT32(5u, accesses(RESETS_DONE));

    /* In order: one poll ends before the next write it guards. */
    TEST_ASSERT_TRUE(last(XOSC_STATUS) < first(CLK_SYS_CTRL));
    TEST_ASSERT_TRUE(last(CLK_SYS_SEL) < first(CLK_REF_CTRL));
    TEST_ASSERT_TRUE(last(CLK_REF_SEL) < first(CLK_REF_DIV));
    TEST_ASSERT_TRUE(last(RESETS_CLR) < first(RESETS_DONE));
    TEST_ASSERT_EQUAL_UINT32(trace_used - 1u, last(RESETS_DONE));
}

static void test_early_moves_clk_sys_off_before_clk_ref_onto_the_crystal(void)
{
    script_bring_up();
    tfw_cm_soc_early();
    trace_close();

    TEST_ASSERT_TRUE(first(CLK_SYS_CTRL) < first(CLK_REF_CTRL));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(CLK_SYS_CTRL));          /* SRC clk_ref */
    TEST_ASSERT_EQUAL_HEX32(2u, reg(CLK_REF_CTRL));          /* SRC xosc */
    TEST_ASSERT_EQUAL_HEX32(1u << 16, reg(CLK_REF_DIV));     /* INT 1 */
    TEST_ASSERT_EQUAL_HEX32(1u << 16, reg(CLK_SYS_DIV));
    TEST_ASSERT_EQUAL_HEX32(1u << 16, reg(CLK_PERI_DIV));
    TEST_ASSERT_EQUAL_HEX32(1u << 11, reg(CLK_PERI_CTRL));   /* ENABLE, clk_sys */
    /* 12 MHz into a 1 MHz tick, for the watchdog and TIMER0. */
    TEST_ASSERT_EQUAL_UINT32(12u, reg(TICKS_WD_CYCLES));
    TEST_ASSERT_EQUAL_HEX32(1u, reg(TICKS_WD_CTRL));
    TEST_ASSERT_EQUAL_UINT32(12u, reg(TICKS_T0_CYCLES));
    TEST_ASSERT_EQUAL_HEX32(1u, reg(TICKS_T0_CTRL));
    TEST_ASSERT_EQUAL_HEX32(RESETS_WANTED, reg(RESETS_CLR));
}

static const uint32_t written[] = {
    WDOG_CTRL_CLR, XOSC_STARTUP, XOSC_CTRL, CLK_SYS_CTRL, CLK_REF_CTRL,
    CLK_REF_DIV, CLK_SYS_DIV, CLK_PERI_DIV, CLK_PERI_CTRL, TICKS_WD_CYCLES,
    TICKS_WD_CTRL, TICKS_T0_CYCLES, TICKS_T0_CTRL, RESETS_CLR,
};
#define WRITTEN (sizeof written / sizeof written[0])

/* Over the state the first run left, every register is written once more with
 * the value it already holds: nothing toggles. */
static void test_a_second_early_writes_the_same_values(void)
{
    uint32_t before[WRITTEN];
    uint32_t i, j;

    script_bring_up();
    tfw_cm_soc_early();
    trace_close();
    for (j = 0u; j < WRITTEN; j++) {
        before[j] = reg(written[j]);
    }

    trace_reset();
    tfw_cm_soc_early();
    trace_close();

    for (j = 0u; j < WRITTEN; j++) {
        TEST_ASSERT_EQUAL_UINT32(1u, accesses(written[j]));
        for (i = 0u; i < trace_used; i++) {
            if (trace_addr[i] == written[j]) {
                TEST_ASSERT_EQUAL_HEX32(before[j], trace_val[i]);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1u, accesses(XOSC_STATUS));
    TEST_ASSERT_EQUAL_UINT32(1u, accesses(CLK_SYS_SEL));
    TEST_ASSERT_EQUAL_UINT32(1u, accesses(CLK_REF_SEL));
    TEST_ASSERT_EQUAL_UINT32(1u, accesses(RESETS_DONE));
}

static void test_watchdog_arm_marks_and_arms_for_8_s(void)
{
    tfw_cm_soc_watchdog_arm();

    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_cm_reset_mark);
    /* Every block but ROSC and XOSC (bits 2, 3), which keep running. */
    TEST_ASSERT_EQUAL_HEX32(0x01FFFFF3u, reg(PSM_WDSEL));
    TEST_ASSERT_EQUAL_UINT32(8000000u, reg(WDOG_LOAD));      /* 1 us ticks */
    TEST_ASSERT_EQUAL_HEX32(0x47000000u, reg(WDOG_CTRL));    /* ENABLE, PAUSE */
    TEST_ASSERT_TRUE(last(WDOG_LOAD) < first(WDOG_CTRL));
}

static void test_watchdog_pet_reloads_load_only(void)
{
    tfw_cm_soc_watchdog_arm();
    *slot(WDOG_LOAD) = 17u;
    trace_used = 0u;

    tfw_cm_soc_watchdog_pet();
    trace_close();

    TEST_ASSERT_EQUAL_UINT32(8000000u, reg(WDOG_LOAD));
    TEST_ASSERT_EQUAL_UINT32(1u, trace_used);
    TEST_ASSERT_EQUAL_HEX32(WDOG_LOAD, trace_addr[0]);
}

/* REASON and CHIP_RESET go through as registers, and only the RESETREQ mark
 * says the firmware asked for the reset. */
static void test_reset_cause_hands_the_registers_and_the_request_on(void)
{
    static const uint32_t marks[] = {0u, TFW_CM_MARK_WATCHDOG, TFW_CM_MARK_PANIC,
                                     TFW_CM_MARK_RESETREQ};
    uint32_t i;

    for (i = 0u; i < sizeof marks / sizeof marks[0]; i++) {
        TEST_ASSERT_EQUAL_INT(TFW_RESET_EXTERNAL, tfw_cm_soc_reset_cause(marks[i]));
        TEST_ASSERT_EQUAL_PTR(slot(WDOG_REASON), take_reason);
        TEST_ASSERT_EQUAL_PTR(slot(POWMAN_CHIP_RST), take_chip_reset);
        TEST_ASSERT_EQUAL_UINT8(marks[i] == TFW_CM_MARK_RESETREQ ? 1u : 0u, take_requested);
    }
    TEST_ASSERT_EQUAL_UINT32(4u, take_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_waits_on_each_status_bit_in_order);
    RUN_TEST(test_early_moves_clk_sys_off_before_clk_ref_onto_the_crystal);
    RUN_TEST(test_a_second_early_writes_the_same_values);
    RUN_TEST(test_watchdog_arm_marks_and_arms_for_8_s);
    RUN_TEST(test_watchdog_pet_reloads_load_only);
    RUN_TEST(test_reset_cause_hands_the_registers_and_the_request_on);
    return UNITY_END();
}
