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

void setUp(void)
{
    uint32_t i;

    model_used = 0u;
    trace_used = 0u;
    for (i = 0u; i < MODEL_POLLS; i++) {
        poll[i].addr = 0u;
    }
    tfw_cm_reset_mark = 0u;
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_waits_on_each_status_bit_in_order);
    return UNITY_END();
}
