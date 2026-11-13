/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l tone: the TIMER arithmetic (tone_calc.h), and hal_tone.c against
 * a register model. As for the i.MX 95 HAL (test/model/imx95_model.c),
 * test/model/cortex-m/cm_regs.h sends every TFW_CM_REG to
 * tfw_imx95_model_reg; this file supplies that function as a model of the
 * nrf54l's TIMER20, GPIOTE20, DPPIC20 and P1 registers. hal_tone.c only
 * writes, so the model hands out a cell holding a sentinel and logs the
 * value written into it at the next access (or at model_flush).
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "tone_calc.h"

/* ------------------------------------------------------------------ model */

#define SENTINEL 0xA5A5A5A5uL
#define LOG_MAX  64u

typedef struct { uintptr_t addr; uint32_t val; } wr_t;

static wr_t              log_w[LOG_MAX];
static unsigned          log_n;
static volatile uint32_t cell;
static uintptr_t         cell_addr;
static uint8_t           cell_live;

static void model_flush(void)
{
    if (cell_live && cell != SENTINEL) {
        TEST_ASSERT_TRUE(log_n < LOG_MAX);
        log_w[log_n].addr = cell_addr;
        log_w[log_n].val  = cell;
        log_n++;
    }
    cell_live = 0u;
}

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    model_flush();
    cell      = SENTINEL;
    cell_addr = addr;
    cell_live = 1u;
    return &cell;
}

uint32_t tfw_imx95_model_irq_save(void) { return 0u; }
void tfw_imx95_model_irq_restore(uint32_t primask) { (void)primask; }
void tfw_imx95_model_irq_mask(uint32_t masked) { (void)masked; }

/* The index of the first write to addr at or after from, or -1. */
static int find_w(uintptr_t addr, unsigned from)
{
    unsigned i;

    for (i = from; i < log_n; i++) {
        if (log_w[i].addr == addr) {
            return (int)i;
        }
    }
    return -1;
}

/* The last value written to addr; fails the test if there was none. */
static uint32_t last_w(uintptr_t addr)
{
    unsigned i = log_n;

    while (i-- > 0u) {
        if (log_w[i].addr == addr) {
            return log_w[i].val;
        }
    }
    TEST_FAIL_MESSAGE("register never written");
    return 0u;
}

#define A_TIMER(off)  ((uintptr_t)TFW_NRF_TIMER_BASE + (off))
#define A_GPIOTE(off) ((uintptr_t)TFW_NRF_GPIOTE_BASE + (off))
#define A_DPPIC(off)  ((uintptr_t)TFW_NRF_DPPIC_BASE + (off))
#define A_P1(off)     ((uintptr_t)TFW_NRF_P1_BASE + (off))

static const tfw_pinref_t LED1 = { .port = 1u, .bit = 10u };

void setUp(void)
{
    (void)tfw_hal_tone_stop();
    model_flush();
    log_n = 0u;
    memset(log_w, 0, sizeof log_w);
}
void tearDown(void) {}

/* ------------------------------------------------------------------- calc */

/* 1 kHz: 16 MHz / (2 * 1000) = 8000 ticks per half period, prescaler 0. */
static void test_tone_calc_1khz(void)
{
    uint32_t pre = 99u, cc = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_nrf54l_tone_calc(1000u, &pre, &cc));
    TEST_ASSERT_EQUAL_UINT32(0u, pre);
    TEST_ASSERT_EQUAL_UINT32(8000u, cc);
}

static void test_tone_calc_table(void)
{
    static const struct { uint32_t hz, pre, cc; } t[] = {
        {     1u, 7u, 62500u },   /* 125 kHz / 2 */
        {    31u, 2u, 64516u },   /* 4 MHz / 62, rounded */
        {   100u, 1u, 40000u },
        {   440u, 0u, 18182u },   /* 18181.8 rounded */
        { 20000u, 0u,   400u },
        { 65535u, 0u,   122u },
        { 4000000u, 0u,   2u },
    };
    unsigned i;

    for (i = 0u; i < sizeof t / sizeof t[0]; i++) {
        uint32_t pre = 99u, cc = 0u;

        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_nrf54l_tone_calc(t[i].hz, &pre, &cc));
        TEST_ASSERT_EQUAL_UINT32(t[i].pre, pre);
        TEST_ASSERT_EQUAL_UINT32(t[i].cc, cc);
    }
}

static void test_tone_calc_range(void)
{
    uint32_t pre = 99u, cc = 77u;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_nrf54l_tone_calc(0u, &pre, &cc));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_nrf54l_tone_calc(6000000u, &pre, &cc));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_nrf54l_tone_calc(8000000u, &pre, &cc));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_nrf54l_tone_calc(8000001u, &pre, &cc));
    TEST_ASSERT_EQUAL_UINT32(99u, pre);   /* untouched on refusal */
    TEST_ASSERT_EQUAL_UINT32(77u, cc);
}

/* -------------------------------------------------------------------- HAL */

static void test_tone_start_configures_in_order(void)
{
    int stop, cc0, pub, cfg, sub, chen, start;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(&LED1, 1000u));
    model_flush();

    stop  = find_w(A_TIMER(0x004u), 0u);
    cc0   = find_w(A_TIMER(0x540u), 0u);
    pub   = find_w(A_TIMER(0x1C0u), 0u);
    cfg   = find_w(A_GPIOTE(0x510u), 0u);
    sub   = find_w(A_GPIOTE(0x080u), 0u);
    chen  = find_w(A_DPPIC(0x504u), 0u);
    start = find_w(A_TIMER(0x000u), 0u);
    TEST_ASSERT_TRUE(stop >= 0 && stop < cc0 && cc0 < pub);
    TEST_ASSERT_TRUE(pub < cfg && cfg < sub);
    TEST_ASSERT_TRUE(sub < chen && chen < start);
    TEST_ASSERT_EQUAL_INT((int)log_n - 1, start);   /* START is the last write */

    TEST_ASSERT_EQUAL_HEX32(0u, last_w(A_TIMER(0x504u)));        /* MODE Timer */
    TEST_ASSERT_EQUAL_HEX32(0u, last_w(A_TIMER(0x508u)));        /* BITMODE 16 */
    TEST_ASSERT_EQUAL_HEX32(0u, last_w(A_TIMER(0x510u)));        /* PRESCALER */
    TEST_ASSERT_EQUAL_HEX32(8000u, last_w(A_TIMER(0x540u)));
    TEST_ASSERT_EQUAL_HEX32(1u, last_w(A_TIMER(0x200u)));        /* COMPARE0_CLEAR */
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, last_w(A_TIMER(0x1C0u))); /* EN, channel 0 */
    /* MODE Task, PSEL 10, PORT 1, POLARITY Toggle, OUTINIT Low. */
    TEST_ASSERT_EQUAL_HEX32(3u | (10u << 4) | (1u << 9) | (3u << 16),
                            last_w(A_GPIOTE(0x510u)));
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, last_w(A_GPIOTE(0x080u)));
    TEST_ASSERT_EQUAL_HEX32(1u, last_w(A_DPPIC(0x504u)));
    TEST_ASSERT_EQUAL_HEX32(1u, last_w(A_TIMER(0x000u)));
}

static void test_tone_retarget(void)
{
    unsigned first;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(&LED1, 1000u));
    model_flush();
    first = log_n;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(&LED1, 100u));
    model_flush();

    /* The first tone is unlinked before the second is set up. */
    TEST_ASSERT_TRUE(find_w(A_DPPIC(0x508u), first) >= 0);
    TEST_ASSERT_TRUE(find_w(A_DPPIC(0x508u), first) < find_w(A_TIMER(0x540u), first));
    TEST_ASSERT_EQUAL_HEX32(1u, last_w(A_TIMER(0x510u)));
    TEST_ASSERT_EQUAL_HEX32(40000u, last_w(A_TIMER(0x540u)));
    TEST_ASSERT_EQUAL_INT((int)log_n - 1, find_w(A_TIMER(0x000u), first));
}

static void test_tone_stop(void)
{
    unsigned from;
    int outclr, cnf, cfg;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(&LED1, 440u));
    model_flush();
    from = log_n;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_stop());
    model_flush();

    TEST_ASSERT_TRUE(find_w(A_TIMER(0x004u), from) >= 0);           /* STOP */
    TEST_ASSERT_EQUAL_HEX32(0u, last_w(A_TIMER(0x1C0u)));             /* unpublished */
    TEST_ASSERT_EQUAL_HEX32(1u, last_w(A_DPPIC(0x508u)));             /* CHENCLR */
    TEST_ASSERT_EQUAL_HEX32(0u, last_w(A_GPIOTE(0x080u)));            /* unsubscribed */
    outclr = find_w(A_P1(0x008u), from);
    cnf    = find_w(A_P1(0x080u + 4u * 10u), from);
    cfg    = find_w(A_GPIOTE(0x510u), from);
    TEST_ASSERT_TRUE(outclr >= 0 && outclr < cfg && cnf < cfg);       /* low, then freed */
    TEST_ASSERT_EQUAL_HEX32(1uL << 10, last_w(A_P1(0x008u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_PIN_CNF_OUTPUT, last_w(A_P1(0x080u + 4u * 10u)));
    TEST_ASSERT_EQUAL_HEX32(0u, last_w(A_GPIOTE(0x510u)));            /* Disabled */

    /* A second stop touches nothing. */
    from = log_n;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_stop());
    model_flush();
    TEST_ASSERT_EQUAL_UINT(from, log_n);
}

static void test_tone_zero_hz_stops(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(&LED1, 1000u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(&LED1, 0u));
    model_flush();
    TEST_ASSERT_EQUAL_HEX32(0u, last_w(A_GPIOTE(0x510u)));
}

static void test_tone_refuses_other_ports(void)
{
    static const tfw_pinref_t p0 = { .port = 0u, .bit = 4u };
    static const tfw_pinref_t p2 = { .port = 2u, .bit = 9u };
    static const tfw_pinref_t nope = { .port = 1u, .bit = 17u };

    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&p0, 1000u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&p2, 1000u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_hal_tone_start(&nope, 1000u));
    model_flush();
    TEST_ASSERT_EQUAL_UINT(0u, log_n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tone_calc_1khz);
    RUN_TEST(test_tone_calc_table);
    RUN_TEST(test_tone_calc_range);
    RUN_TEST(test_tone_start_configures_in_order);
    RUN_TEST(test_tone_retarget);
    RUN_TEST(test_tone_stop);
    RUN_TEST(test_tone_zero_hz_stops);
    RUN_TEST(test_tone_refuses_other_ports);
    return UNITY_END();
}
