/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN947's early clock and WWDT0 (hal_soc.c), over a register model.
 * boot/hal/cortex-m/rp2350/test/cm_regs.h comes ahead of the real one, so
 * every TFW_CM_REG lands in tfw_cm_model_reg below, and the interrupt mask is
 * the test's. Each access calls the model once, so the model keeps a trace:
 * an access is logged, with the register's value after it, when the next
 * access (or a mask call, or the test) comes. SCG's CSR shows SIRC only on
 * the read the test names, so the switch is seen to wait on CSR.
 *
 * The directory is beside the SoC's sources because it names their
 * registers; the SoC's *.c wildcard does not descend into it, so no image
 * links it.
 */
#include <stdint.h>
#include "unity.h"
#include "cm_regs.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "soc_reset.h"

volatile uint32_t tfw_cm_reset_mark;

#define MODEL_REGS 32u
#define TRACE_LEN  64u

#define SCS_SIRC (TFW_MCXN_SCG_SCS_SIRC << TFW_MCXN_SCG_SCS_SHIFT)

/* The addresses, by the model's own arithmetic on soc_regs.h's offsets. */
#define A_PRESETCTRLCLR0 (TFW_MCXN_SYSCON_BASE + 0x140u)
#define A_AHBCLKCTRLSET0 (TFW_MCXN_SYSCON_BASE + 0x220u)
#define A_AHBCLKCTRLSET2 (TFW_MCXN_SYSCON_BASE + 0x228u)
#define A_AHBCLKDIV      (TFW_MCXN_SYSCON_BASE + 0x380u)
#define A_WDT0CLKDIV     (TFW_MCXN_SYSCON_BASE + 0x38Cu)
#define A_CLOCK_CTRL     (TFW_MCXN_SYSCON_BASE + 0xA18u)
#define A_ECC            (TFW_MCXN_SYSCON_BASE + 0xE44u)
#define A_SCG_CSR        (TFW_MCXN_SCG_BASE + 0x010u)
#define A_SCG_RCCR       (TFW_MCXN_SCG_BASE + 0x014u)
#define A_SCG_SIRCCSR    (TFW_MCXN_SCG_BASE + 0x200u)
#define A_WWDT_MOD       (TFW_MCXN_WWDT_BASE + 0x00u)
#define A_WWDT_TC        (TFW_MCXN_WWDT_BASE + 0x04u)
#define A_WWDT_FEED      (TFW_MCXN_WWDT_BASE + 0x08u)
#define A_CMC_SRS        (TFW_MCXN_CMC_BASE + 0x80u)

enum ev_kind { EV_REG, EV_SAVE, EV_RESTORE };

struct ev {
    enum ev_kind kind;
    uint32_t     addr;
    uint32_t     val;
};

static uint32_t model_addr[MODEL_REGS];
static volatile uint32_t model_val[MODEL_REGS];
static uint32_t model_used;

static struct ev trace[TRACE_LEN];
static uint32_t  trace_used;
static int32_t   pending = -1;

static uint32_t csr_reads;
static uint32_t csr_sirc_at;

static void trace_add(enum ev_kind kind, uint32_t addr, uint32_t val)
{
    if (trace_used == TRACE_LEN) {
        TEST_FAIL_MESSAGE("trace full");
    }
    trace[trace_used].kind = kind;
    trace[trace_used].addr = addr;
    trace[trace_used].val = val;
    trace_used++;
}

/* Logs the access the model last answered, with the value it left. */
static void flush(void)
{
    if (pending >= 0) {
        trace_add(EV_REG, model_addr[pending], model_val[pending]);
        pending = -1;
    }
}

static uint32_t slot(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return i;
        }
    }
    if (model_used == MODEL_REGS) {
        TEST_FAIL_MESSAGE("register model full");
    }
    model_addr[model_used] = addr;
    model_val[model_used] = 0u;
    return model_used++;
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    uint32_t i;

    flush();
    i = slot(addr);
    if (addr == A_SCG_CSR) {
        csr_reads++;
        if (csr_reads == csr_sirc_at) {
            model_val[i] = (model_val[i] & ~TFW_MCXN_SCG_SCS_MASK) | SCS_SIRC;
        }
    }
    pending = (int32_t)i;
    return &model_val[i];
}

static void seed(uint32_t addr, uint32_t val)
{
    model_val[slot(addr)] = val;
}

/* The index of the first access to addr at or after from, or -1. */
static int32_t find(uint32_t addr, uint32_t from)
{
    uint32_t i;

    flush();
    for (i = from; i < trace_used; i++) {
        if (trace[i].kind == EV_REG && trace[i].addr == addr) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* The value the last access to addr left. */
static uint32_t last(uint32_t addr)
{
    uint32_t i;

    flush();
    for (i = trace_used; i > 0u; i--) {
        if (trace[i - 1u].kind == EV_REG && trace[i - 1u].addr == addr) {
            return trace[i - 1u].val;
        }
    }
    TEST_FAIL_MESSAGE("register never accessed");
    return 0u;
}

static uint32_t count(uint32_t addr)
{
    uint32_t i, n = 0u;

    flush();
    for (i = 0u; i < trace_used; i++) {
        if (trace[i].kind == EV_REG && trace[i].addr == addr) {
            n++;
        }
    }
    return n;
}

/* The mask: save logs itself and hands out a value restore must get back. */
#define PRIMASK_TOKEN 0x5A17u

static uint32_t irq_saves;
static uint32_t irq_restored;

uint32_t tfw_cm_irq_save(void)
{
    flush();
    trace_add(EV_SAVE, 0u, 0u);
    irq_saves++;
    return PRIMASK_TOKEN + irq_saves;
}

void tfw_cm_irq_restore(uint32_t primask)
{
    flush();
    trace_add(EV_RESTORE, 0u, primask);
    irq_restored = primask;
}

/* soc_reset.c's decode, stood in for: the test checks what it is handed. */
static const volatile uint32_t *take_srs;
static uint8_t take_requested;
static uint32_t take_calls;

tfw_reset_cause_t tfw_mcxn_reset_take(const volatile uint32_t *srs, uint8_t requested)
{
    take_srs = srs;
    take_requested = requested;
    take_calls++;
    return TFW_RESET_WATCHDOG;
}

void setUp(void)
{
    irq_saves = 0u;
    irq_restored = 0u;
    take_srs = 0;
    take_requested = 0xFFu;
    take_calls = 0u;
    model_used = 0u;
    trace_used = 0u;
    pending = -1;
    csr_reads = 0u;
    csr_sirc_at = 1u;
    tfw_cm_reset_mark = 0u;
}

void tearDown(void)
{
}

static void test_early_switches_to_sirc_and_waits_on_csr(void)
{
    uint32_t rccr_other = 0x00000003u;
    uint32_t sirc_other = 0x01000001u;
    int32_t ecc, scg, sirc, rccr, csr;

    seed(A_ECC, 0xFFFFFFFFu);
    seed(A_SCG_SIRCCSR, sirc_other);
    seed(A_SCG_RCCR, rccr_other | (3u << TFW_MCXN_SCG_SCS_SHIFT));
    seed(A_SCG_CSR, 3u << TFW_MCXN_SCG_SCS_SHIFT);
    csr_sirc_at = 4u;
    tfw_cm_soc_early();

    ecc = find(A_ECC, 0u);
    TEST_ASSERT_EQUAL_INT32(0, ecc);
    TEST_ASSERT_EQUAL_HEX32(0u, trace[ecc].val);

    scg = find(A_AHBCLKCTRLSET2, 0u);
    sirc = find(A_SCG_SIRCCSR, 0u);
    rccr = find(A_SCG_RCCR, 0u);
    csr = find(A_SCG_CSR, 0u);
    TEST_ASSERT_TRUE(scg > ecc);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CTRL2_SCG, trace[scg].val);
    TEST_ASSERT_TRUE(sirc > scg);
    TEST_ASSERT_EQUAL_HEX32(sirc_other | TFW_MCXN_SCG_SIRC_PERIPH_EN, last(A_SCG_SIRCCSR));
    TEST_ASSERT_TRUE(rccr > sirc);
    TEST_ASSERT_EQUAL_HEX32(rccr_other | SCS_SIRC, last(A_SCG_RCCR));
    TEST_ASSERT_TRUE(csr > rccr);

    /* The poll reads CSR until SIRC shows, and not once more. */
    TEST_ASSERT_EQUAL_UINT32(4u, csr_reads);
    TEST_ASSERT_EQUAL_UINT32(4u, count(A_SCG_CSR));
    TEST_ASSERT_EQUAL_INT32(csr + 3, find(A_SCG_CSR, (uint32_t)csr + 3u));
    TEST_ASSERT_TRUE(find(A_AHBCLKDIV, 0u) > csr + 3);
}

static void test_early_restarts_the_ahb_divider_at_one(void)
{
    int32_t div;

    seed(A_AHBCLKDIV, 0x7u);
    tfw_cm_soc_early();

    div = find(A_AHBCLKDIV, 0u);
    TEST_ASSERT_TRUE(div >= 0);
    TEST_ASSERT_EQUAL_UINT32(2u, count(A_AHBCLKDIV));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT, trace[div].val);
    TEST_ASSERT_EQUAL_INT32(div + 1, find(A_AHBCLKDIV, (uint32_t)div + 1u));
    TEST_ASSERT_EQUAL_HEX32(0u, trace[div + 1].val);
}

static void test_early_clocks_and_releases_every_port_and_gpio_bank(void)
{
    uint32_t banks = 0u;
    uint32_t n;
    int32_t set, clr;

    for (n = 0u; n < 5u; n++) {
        banks |= (1u << (13u + n)) | (1u << (19u + n));
    }
    tfw_cm_soc_early();

    set = find(A_AHBCLKCTRLSET0, 0u);
    clr = find(A_PRESETCTRLCLR0, 0u);
    TEST_ASSERT_TRUE(set > find(A_AHBCLKDIV, 0u));
    TEST_ASSERT_TRUE(clr > set);
    TEST_ASSERT_EQUAL_HEX32(banks, trace[set].val);
    TEST_ASSERT_EQUAL_HEX32(banks, trace[clr].val);
    TEST_ASSERT_EQUAL_UINT32(1u, count(A_AHBCLKCTRLSET0));
    TEST_ASSERT_EQUAL_UINT32(1u, count(A_PRESETCTRLCLR0));
}

/* One feed at from: SAVE, FEED_1, FEED_2, RESTORE with save's value, and
 * nothing between them. Returns the index after it. */
static uint32_t assert_feed_at(uint32_t from)
{
    flush();
    TEST_ASSERT_TRUE(from + 4u <= trace_used);
    TEST_ASSERT_EQUAL_INT(EV_SAVE, trace[from].kind);
    TEST_ASSERT_EQUAL_INT(EV_REG, trace[from + 1u].kind);
    TEST_ASSERT_EQUAL_HEX32(A_WWDT_FEED, trace[from + 1u].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_WWDT_FEED_1, trace[from + 1u].val);
    TEST_ASSERT_EQUAL_INT(EV_REG, trace[from + 2u].kind);
    TEST_ASSERT_EQUAL_HEX32(A_WWDT_FEED, trace[from + 2u].addr);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_WWDT_FEED_2, trace[from + 2u].val);
    TEST_ASSERT_EQUAL_INT(EV_RESTORE, trace[from + 3u].kind);
    TEST_ASSERT_EQUAL_HEX32(PRIMASK_TOKEN + irq_saves, trace[from + 3u].val);
    return from + 4u;
}

static void test_arm_marks_clocks_and_starts_wwdt0_then_feeds(void)
{
    uint32_t clock_other = 0x00000101u;
    int32_t ctrl, set, div, tc, mod;

    seed(A_CLOCK_CTRL, clock_other);
    seed(A_WDT0CLKDIV, 0x3u);
    tfw_cm_soc_watchdog_arm();

    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MARK_WATCHDOG, tfw_cm_reset_mark);
    ctrl = find(A_CLOCK_CTRL, 0u);
    set = find(A_AHBCLKCTRLSET0, 0u);
    div = find(A_WDT0CLKDIV, 0u);
    tc = find(A_WWDT_TC, 0u);
    mod = find(A_WWDT_MOD, 0u);
    TEST_ASSERT_EQUAL_INT32(0, ctrl);
    TEST_ASSERT_EQUAL_HEX32(clock_other | TFW_MCXN_CLOCK_CTRL_FRO1MHZ_ENA |
                            TFW_MCXN_CLOCK_CTRL_FRO1MHZ_CLK_ENA, trace[ctrl].val);
    TEST_ASSERT_TRUE(set > ctrl);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CTRL0_WWDT0, trace[set].val);
    TEST_ASSERT_TRUE(div > set);
    TEST_ASSERT_EQUAL_UINT32(2u, count(A_WDT0CLKDIV));
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT, trace[div].val);
    TEST_ASSERT_EQUAL_INT32(div + 1, find(A_WDT0CLKDIV, (uint32_t)div + 1u));
    TEST_ASSERT_EQUAL_HEX32(0u, trace[div + 1].val);
    TEST_ASSERT_TRUE(tc > div + 1);
    TEST_ASSERT_EQUAL_HEX32(2000000u, trace[tc].val);
    TEST_ASSERT_EQUAL_INT32(tc + 1, mod);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_WWDT_MOD_WDEN | TFW_MCXN_WWDT_MOD_WDRESET, trace[mod].val);
    TEST_ASSERT_EQUAL_UINT32(1u, count(A_WWDT_TC));
    TEST_ASSERT_EQUAL_UINT32(1u, count(A_WWDT_MOD));

    /* Then one feed, and nothing after it. */
    TEST_ASSERT_EQUAL_UINT32(1u, irq_saves);
    TEST_ASSERT_EQUAL_UINT32(trace_used, assert_feed_at((uint32_t)mod + 1u));
    TEST_ASSERT_EQUAL_UINT32(2u, count(A_WWDT_FEED));
}

static void test_pet_is_one_feed_under_masked_interrupts(void)
{
    tfw_cm_soc_watchdog_pet();
    TEST_ASSERT_EQUAL_UINT32(1u, irq_saves);
    TEST_ASSERT_EQUAL_UINT32(trace_used, assert_feed_at(0u));
    TEST_ASSERT_EQUAL_HEX32(PRIMASK_TOKEN + 1u, irq_restored);

    /* A second pet is a second whole feed with its own mask. */
    tfw_cm_soc_watchdog_pet();
    TEST_ASSERT_EQUAL_UINT32(2u, irq_saves);
    TEST_ASSERT_EQUAL_UINT32(trace_used, assert_feed_at(4u));
    TEST_ASSERT_EQUAL_HEX32(PRIMASK_TOKEN + 2u, irq_restored);
}

static void test_reset_cause_hands_srs_and_the_resetreq_flag_on(void)
{
    volatile uint32_t *srs = tfw_cm_model_reg(A_CMC_SRS);

    TEST_ASSERT_EQUAL_INT(TFW_RESET_WATCHDOG, tfw_cm_soc_reset_cause(TFW_CM_MARK_RESETREQ));
    TEST_ASSERT_EQUAL_UINT32(1u, take_calls);
    TEST_ASSERT_EQUAL_PTR(srs, take_srs);
    TEST_ASSERT_EQUAL_UINT8(1u, take_requested);

    (void)tfw_cm_soc_reset_cause(TFW_CM_MARK_WATCHDOG);
    TEST_ASSERT_EQUAL_PTR(srs, take_srs);
    TEST_ASSERT_EQUAL_UINT8(0u, take_requested);

    (void)tfw_cm_soc_reset_cause(0u);
    TEST_ASSERT_EQUAL_UINT8(0u, take_requested);
    TEST_ASSERT_EQUAL_UINT32(3u, take_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_switches_to_sirc_and_waits_on_csr);
    RUN_TEST(test_early_restarts_the_ahb_divider_at_one);
    RUN_TEST(test_early_clocks_and_releases_every_port_and_gpio_bank);
    RUN_TEST(test_arm_marks_clocks_and_starts_wwdt0_then_feeds);
    RUN_TEST(test_pet_is_one_feed_under_masked_interrupts);
    RUN_TEST(test_reset_cause_hands_srs_and_the_resetreq_flag_on);
    return UNITY_END();
}
