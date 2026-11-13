/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x's hal_soc.c register values (src/hal/cortex-m/mcxn94x/
 * hal_soc.c, soc_regs.h) on the register file (test/model/cortex-m/
 * regfile_model.c): tfw_cm_soc_early leaves RAM ECC off, the main clock on
 * FRO12M with the AHB divider at 1 and the five pin banks clocked and out of
 * reset, and touches no WWDT0 register; tfw_cm_soc_watchdog_arm marks the
 * reset, runs the 1 MHz FRO into WWDT0, loads TC and MOD and feeds 0xAA then
 * 0x55; tfw_cm_soc_watchdog_pet is the feed alone. A board has not run them.
 *
 * hal_soc.c is included here, after cm_regs.h, so its TFW_CM_REG goes to
 * soc_reg: the WWDT0 registers and the two dividers get a fresh word per
 * access, which keeps each write in order (a feed is two writes to one
 * register, and a divider is halted before its count goes in); every other
 * register is the register file's. The model's PRIMASK is a stub, so the
 * feed's masked interrupts are not checked here.
 */
#include <stdint.h>

#include "unity.h"
#include "cm_regs.h"
#include "regfile_model.h"

static volatile uint32_t *soc_reg(uintptr_t addr);
#undef TFW_CM_REG
#define TFW_CM_REG(addr) (*soc_reg((uintptr_t)(addr)))

#include "../../src/hal/cortex-m/mcxn94x/hal_soc.c"

/* hal_early.c's word in .resetmark, which this suite does not link. */
volatile uint32_t tfw_cm_reset_mark;

#define SYSCON(off)        ((uintptr_t)TFW_MCXN_SYSCON_BASE + (off))
#define SCG(off)           ((uintptr_t)TFW_MCXN_SCG_BASE + (off))
#define WWDT(off)          ((uintptr_t)TFW_MCXN_WWDT_BASE + (off))

#define PRESETCTRLCLR0     SYSCON(0x140u)
#define AHBCLKCTRLSET0     SYSCON(0x220u)
#define AHBCLKCTRLSET2     SYSCON(0x228u)
#define AHBCLKDIV          SYSCON(0x380u)
#define WDT0CLKDIV         SYSCON(0x38Cu)
#define CLOCK_CTRL         SYSCON(0xA18u)
#define ECC_ENABLE_CTRL    SYSCON(0xE44u)
#define SCG_RCCR           SCG(0x014u)
#define SCG_SIRCCSR        SCG(0x200u)
#define WWDT_MOD           WWDT(0x00u)
#define WWDT_TC            WWDT(0x04u)
#define WWDT_FEED          WWDT(0x08u)

/* A divider write halts and resets it first (CLOCK_SetClkDiv), then the
 * count goes in: DIV 0 divides by 1. */
#define DIV_HALT_RESET     0x60000000u

#define WLOG_MAX 32u

static struct {
    uintptr_t         addr;
    volatile uint32_t val;
} wlog[WLOG_MAX];
static unsigned wlog_n;

static int is_logged(uintptr_t addr)
{
    return (addr >= WWDT(0u) && addr < WWDT(0x1000u)) || addr == AHBCLKDIV ||
           addr == WDT0CLKDIV;
}

static volatile uint32_t *soc_reg(uintptr_t addr)
{
    if (!is_logged(addr)) {
        return tfw_cm_model_reg(addr);
    }
    TEST_ASSERT_TRUE_MESSAGE(wlog_n < WLOG_MAX, "write log full");
    wlog[wlog_n].addr = addr;
    wlog[wlog_n].val  = 0u;
    return &wlog[wlog_n++].val;
}

/* The values written to addr, in order; the count. */
static unsigned writes_to(uintptr_t addr, uint32_t *out, unsigned max)
{
    unsigned i;
    unsigned n = 0u;

    for (i = 0u; i < wlog_n; i++) {
        if (wlog[i].addr == addr) {
            if (n < max) {
                out[n] = wlog[i].val;
            }
            n++;
        }
    }
    return n;
}

static unsigned wwdt_accesses(void)
{
    unsigned i;
    unsigned n = 0u;

    for (i = 0u; i < wlog_n; i++) {
        if (wlog[i].addr >= WWDT(0u) && wlog[i].addr < WWDT(0x1000u)) {
            n++;
        }
    }
    return n;
}

static void assert_logged(unsigned i, uintptr_t addr, uint32_t val)
{
    TEST_ASSERT_TRUE(i < wlog_n);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)addr, (uint32_t)wlog[i].addr);
    TEST_ASSERT_EQUAL_HEX32(val, wlog[i].val);
}

/* The SCG's CSR shows the source RCCR asks for once it has switched; the
 * register file cannot, so it reads FRO12M before the call. */
static void run_early(void)
{
    TFW_MCXN_SCG_CSR = TFW_MCXN_SCG_SCS_SIRC << TFW_MCXN_SCG_SCS_SHIFT;
    tfw_cm_soc_early();
}

void setUp(void)
{
    regfile_model_reset();
    wlog_n            = 0u;
    tfw_cm_reset_mark = 0u;
}

void tearDown(void)
{
}

void test_early_turns_ram_ecc_off(void)
{
    TFW_MCXN_ECC_ENABLE_CTRL = 0xFFFFFFFFu;
    run_early();
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(ECC_ENABLE_CTRL));
}

void test_early_opens_the_scg_and_its_fro12m_peripheral_clock(void)
{
    TFW_MCXN_SCG_SIRCCSR = 0x1u;
    run_early();
    /* AHBCLKCTRL2 bit 26, the SCG; SIRCCSR bit 5, SIRC_CLK_PERIPH_EN, the
     * other bits kept. */
    TEST_ASSERT_EQUAL_HEX32(0x04000000u, regfile_model_peek(AHBCLKCTRLSET2));
    TEST_ASSERT_EQUAL_HEX32(0x00000021u, regfile_model_peek(SCG_SIRCCSR));
}

void test_early_runs_the_core_from_fro12m(void)
{
    /* SCS 3 (another source) and bits of RCCR's that are not SCS. */
    TFW_MCXN_SCG_RCCR = 0x03000011u;
    run_early();
    TEST_ASSERT_EQUAL_HEX32(0x02000011u, regfile_model_peek(SCG_RCCR));
    TEST_ASSERT_EQUAL_UINT32(12000000uL, TFW_CM_CPU_HZ);
}

void test_early_sets_the_ahb_divider_to_1_after_halting_it(void)
{
    uint32_t v[2];

    run_early();
    TEST_ASSERT_EQUAL_UINT(2u, writes_to(AHBCLKDIV, v, 2u));
    TEST_ASSERT_EQUAL_HEX32(DIV_HALT_RESET, v[0]);
    TEST_ASSERT_EQUAL_HEX32(0u, v[1]);
}

void test_early_clocks_and_releases_the_five_pin_banks(void)
{
    run_early();
    /* PORT0..4 at bits 13..17, GPIO0..4 at bits 19..23. */
    TEST_ASSERT_EQUAL_HEX32(0x00FBE000u, regfile_model_peek(AHBCLKCTRLSET0));
    TEST_ASSERT_EQUAL_HEX32(0x00FBE000u, regfile_model_peek(PRESETCTRLCLR0));
}

void test_early_leaves_the_watchdog_alone(void)
{
    uint32_t v[1];

    run_early();
    TEST_ASSERT_EQUAL_UINT(0u, wwdt_accesses());
    TEST_ASSERT_EQUAL_UINT(0u, writes_to(WDT0CLKDIV, v, 1u));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(CLOCK_CTRL));
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_cm_reset_mark);
}

void test_arm_marks_the_next_reset_as_the_watchdogs(void)
{
    tfw_cm_soc_watchdog_arm();
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_cm_reset_mark);
}

void test_arm_runs_the_1mhz_fro_into_wwdt0(void)
{
    uint32_t v[2];

    TFW_MCXN_CLOCK_CTRL = 0x1u;
    tfw_cm_soc_watchdog_arm();
    /* CLOCK_CTRL FRO1MHZ_ENA (bit 2) and FRO1MHZ_CLK_ENA (bit 6), the other
     * bits kept; AHBCLKCTRL0 bit 28, WWDT0; its divider at 1. */
    TEST_ASSERT_EQUAL_HEX32(0x00000045u, regfile_model_peek(CLOCK_CTRL));
    TEST_ASSERT_EQUAL_HEX32(0x10000000u, regfile_model_peek(AHBCLKCTRLSET0));
    TEST_ASSERT_EQUAL_UINT(2u, writes_to(WDT0CLKDIV, v, 2u));
    TEST_ASSERT_EQUAL_HEX32(DIV_HALT_RESET, v[0]);
    TEST_ASSERT_EQUAL_HEX32(0u, v[1]);
}

void test_arm_loads_tc_and_mod_then_feeds(void)
{
    unsigned i = 0u;

    tfw_cm_soc_watchdog_arm();
    /* The divider writes come first; then WWDT0 alone, in this order. */
    while (i < wlog_n && wlog[i].addr == WDT0CLKDIV) {
        i++;
    }
    TEST_ASSERT_EQUAL_UINT(4u, wwdt_accesses());
    /* 2,000,000 counts of 1 MHz / 4: 8 s. */
    assert_logged(i + 0u, WWDT_TC, 2000000u);
    /* WDEN (bit 0) and WDRESET (bit 1). */
    assert_logged(i + 1u, WWDT_MOD, 0x3u);
    assert_logged(i + 2u, WWDT_FEED, 0xAAu);
    assert_logged(i + 3u, WWDT_FEED, 0x55u);
    TEST_ASSERT_EQUAL_UINT(i + 4u, wlog_n);
}

void test_pet_is_the_feed_alone(void)
{
    tfw_cm_soc_watchdog_pet();
    TEST_ASSERT_EQUAL_UINT(2u, wlog_n);
    assert_logged(0u, WWDT_FEED, 0xAAu);
    assert_logged(1u, WWDT_FEED, 0x55u);
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_cm_reset_mark);
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(CLOCK_CTRL));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(AHBCLKCTRLSET0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_turns_ram_ecc_off);
    RUN_TEST(test_early_opens_the_scg_and_its_fro12m_peripheral_clock);
    RUN_TEST(test_early_runs_the_core_from_fro12m);
    RUN_TEST(test_early_sets_the_ahb_divider_to_1_after_halting_it);
    RUN_TEST(test_early_clocks_and_releases_the_five_pin_banks);
    RUN_TEST(test_early_leaves_the_watchdog_alone);
    RUN_TEST(test_arm_marks_the_next_reset_as_the_watchdogs);
    RUN_TEST(test_arm_runs_the_1mhz_fro_into_wwdt0);
    RUN_TEST(test_arm_loads_tc_and_mod_then_feeds);
    RUN_TEST(test_pet_is_the_feed_alone);
    return UNITY_END();
}
