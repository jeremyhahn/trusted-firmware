/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mps2 SoC's watchdog code (src/hal/cortex-m/mps2/hal_soc.c) on a host
 * register file: tfw_cm_soc_early, tfw_cm_soc_watchdog_arm and
 * tfw_cm_soc_watchdog_pet, compiled for mps2-an505 (20 MHz APB), and the
 * value each leaves in the CMSDK APB watchdog's registers, checked against
 * the watchdog's programmers model in the Cortex-M System Design Kit TRM
 * (Arm DDI 0479):
 *
 *   LOAD   +0x000  the count; a write reloads the counter
 *   CTRL   +0x008  bit 0 INTEN (also runs the counter), bit 1 RESEN
 *   INTCLR +0x00C  any write clears the interrupt and reloads the counter
 *   LOCK   +0xC00  0x1ACCE551 enables writes to the others, anything else
 *                  disables them
 *
 * So early disarms (CTRL 0), arm loads 4 s of APB clock and sets INTEN and
 * RESEN, pet writes INTCLR, and each unlocks first and locks again after.
 *
 * The register file is this suite's: TFW_CM_REG hands out one cell per
 * access and the next access (or settle()) takes what was stored in it as a
 * write to that address, which the file applies as the watchdog would,
 * ignoring a write while locked. hal_soc.c only stores, so a cell left at
 * NOT_WRITTEN is a read, not a write. hal_soc.c is included here, after the
 * file's TFW_CM_REG, so its soc_regs.h names compile unchanged onto it.
 *
 * These run on the host only. A board has not run them.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"

#define NOT_WRITTEN 0xA5A5A5A5u

static volatile uint32_t *mps2_reg(uintptr_t addr);

/* The real cm_regs.h is kept out: its TFW_CM_REG is a pointer to the
 * address itself. */
#define TFW_CM_REGS_H
#define TFW_CM_REG(addr) (*mps2_reg((uintptr_t)(addr)))

#include "hal_soc.c"

_Static_assert(TFW_CM_CPU_HZ == 20000000uL,
               "the suite is built for mps2-an505, whose APB runs at 20 MHz");

#define WDOG_KEY     0x1ACCE551u
#define LOAD_4S      80000000u /* 4 s at 20 MHz */
#define OFF_LOAD     0x000u
#define OFF_CTRL     0x008u
#define OFF_INTCLR   0x00Cu
#define OFF_LOCK     0xC00u

#define LOG_MAX 32u

struct wr {
    uint32_t off;
    uint32_t val;
    int      ignored;
};

static struct {
    uint32_t  load, value, ctrl;
    int       ris;     /* the interrupt raised */
    int       locked;
    struct wr log[LOG_MAX];
    unsigned  n;
    unsigned  ignored;
    unsigned  unmodelled;
} wd;

static uint32_t          cell = NOT_WRITTEN;
static uintptr_t         cell_addr;
static int               cell_live;

volatile uint32_t tfw_cm_reset_mark;

static uint32_t decoded_mark;
tfw_reset_cause_t tfw_cm_mark_decode(uint32_t mark)
{
    decoded_mark = mark;
    return (tfw_reset_cause_t)0;
}

static void wd_write(uintptr_t addr, uint32_t val)
{
    uint32_t off = (uint32_t)(addr - TFW_MPS2_WDOG_BASE);
    int ignored = 0;

    if (addr < TFW_MPS2_WDOG_BASE || addr >= TFW_MPS2_WDOG_BASE + 0x1000u) {
        wd.unmodelled++;
        return;
    }
    if (off == OFF_LOCK) {
        wd.locked = (val != WDOG_KEY);
    } else if (wd.locked) {
        ignored = 1;
        wd.ignored++;
    } else if (off == OFF_LOAD) {
        wd.load  = val;
        wd.value = val;
    } else if (off == OFF_CTRL) {
        wd.ctrl = val & 0x3u;
    } else if (off == OFF_INTCLR) {
        wd.ris   = 0;
        wd.value = wd.load;
    } else {
        wd.unmodelled++;
    }
    if (wd.n < LOG_MAX) {
        wd.log[wd.n].off     = off;
        wd.log[wd.n].val     = val;
        wd.log[wd.n].ignored = ignored;
        wd.n++;
    }
}

static void settle(void)
{
    if (cell_live && cell != NOT_WRITTEN)
        wd_write(cell_addr, cell);
    cell_live = 0;
    cell      = NOT_WRITTEN;
}

static volatile uint32_t *mps2_reg(uintptr_t addr)
{
    settle();
    cell_addr = addr;
    cell_live = 1;
    return &cell;
}

/* A watchdog an earlier image left running: armed, locked, its interrupt
 * raised and the count nearly out. */
static void preset_running(void)
{
    wd.load   = 1234u;
    wd.value  = 7u;
    wd.ctrl   = TFW_MPS2_WDOG_CTRL_INTEN | TFW_MPS2_WDOG_CTRL_RESEN;
    wd.ris    = 1;
    wd.locked = 1;
}

static void clear_log(void)
{
    wd.n = 0;
    wd.ignored = 0;
    wd.unmodelled = 0;
}

void setUp(void)
{
    memset(&wd, 0, sizeof wd);
    cell = NOT_WRITTEN;
    cell_live = 0;
    tfw_cm_reset_mark = 0;
    decoded_mark = 0;
}

void tearDown(void) {}

static void assert_bracketed(void)
{
    TEST_ASSERT_TRUE(wd.n >= 2u);
    TEST_ASSERT_EQUAL_HEX32(OFF_LOCK, wd.log[0].off);
    TEST_ASSERT_EQUAL_HEX32(WDOG_KEY, wd.log[0].val);
    TEST_ASSERT_EQUAL_HEX32(OFF_LOCK, wd.log[wd.n - 1u].off);
    TEST_ASSERT_NOT_EQUAL(WDOG_KEY, wd.log[wd.n - 1u].val);
    TEST_ASSERT_TRUE(wd.locked);
    TEST_ASSERT_EQUAL_UINT(0u, wd.ignored);
    TEST_ASSERT_EQUAL_UINT(0u, wd.unmodelled);
}

static void test_early_disarms_a_running_watchdog_through_the_lock(void)
{
    preset_running();
    tfw_cm_soc_early();
    settle();

    TEST_ASSERT_EQUAL_HEX32(0u, wd.ctrl);
    TEST_ASSERT_FALSE(wd.ris);
    TEST_ASSERT_EQUAL_HEX32(1234u, wd.load);
    assert_bracketed();
}

static void test_early_writes_ctrl_zero_then_intclr(void)
{
    preset_running();
    tfw_cm_soc_early();
    settle();

    TEST_ASSERT_EQUAL_UINT(4u, wd.n);
    TEST_ASSERT_EQUAL_HEX32(OFF_CTRL, wd.log[1].off);
    TEST_ASSERT_EQUAL_HEX32(0u, wd.log[1].val);
    TEST_ASSERT_EQUAL_HEX32(OFF_INTCLR, wd.log[2].off);
}

static void test_arm_loads_four_seconds_of_apb_clock(void)
{
    tfw_cm_soc_early();
    settle();
    clear_log();

    tfw_cm_soc_watchdog_arm();
    settle();

    TEST_ASSERT_EQUAL_HEX32(LOAD_4S, wd.load);
    TEST_ASSERT_EQUAL_HEX32(LOAD_4S, wd.value);
    assert_bracketed();
}

static void test_arm_enables_the_interrupt_and_the_reset(void)
{
    tfw_cm_soc_early();
    settle();
    clear_log();

    tfw_cm_soc_watchdog_arm();
    settle();

    TEST_ASSERT_EQUAL_HEX32(0x3u, wd.ctrl);
    TEST_ASSERT_FALSE(wd.ris);
    assert_bracketed();
}

static void test_arm_loads_before_it_enables(void)
{
    tfw_cm_soc_watchdog_arm();
    settle();

    /* the key, LOAD, INTCLR, CTRL, the lock */
    TEST_ASSERT_EQUAL_UINT(5u, wd.n);
    TEST_ASSERT_EQUAL_HEX32(OFF_LOAD, wd.log[1].off);
    TEST_ASSERT_EQUAL_HEX32(OFF_INTCLR, wd.log[2].off);
    TEST_ASSERT_EQUAL_HEX32(OFF_CTRL, wd.log[3].off);
}

static void test_arm_marks_the_reset_as_the_watchdog(void)
{
    tfw_cm_soc_watchdog_arm();
    settle();

    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_cm_reset_mark);
}

static void test_pet_clears_the_interrupt_and_reloads(void)
{
    tfw_cm_soc_watchdog_arm();
    settle();
    wd.value = 5u; /* nearly out, the interrupt raised */
    wd.ris   = 1;
    clear_log();

    tfw_cm_soc_watchdog_pet();
    settle();

    TEST_ASSERT_FALSE(wd.ris);
    TEST_ASSERT_EQUAL_HEX32(LOAD_4S, wd.value);
    TEST_ASSERT_EQUAL_HEX32(LOAD_4S, wd.load);
    TEST_ASSERT_EQUAL_HEX32(0x3u, wd.ctrl);
    TEST_ASSERT_EQUAL_UINT(3u, wd.n);
    TEST_ASSERT_EQUAL_HEX32(OFF_INTCLR, wd.log[1].off);
    assert_bracketed();
}

static void test_reset_cause_is_the_marker_decoded(void)
{
    (void)tfw_cm_soc_reset_cause(TFW_CM_MARK_WATCHDOG);

    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, decoded_mark);
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
