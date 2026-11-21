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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_early_switches_to_sirc_and_waits_on_csr);
    return UNITY_END();
}
