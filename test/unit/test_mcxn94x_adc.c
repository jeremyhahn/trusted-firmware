/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x LPADC0 HAL (src/hal/cortex-m/mcxn94x/hal_adc.c) against a
 * register model: the model header under test/model/ sends every access
 * here, where it is logged in order and the status reads are answered as the
 * part would answer them. The addresses are literals from the reference
 * manual's memory map, not soc_regs.h's macros, so a wrong offset there shows.
 */

#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"

/* The model's entry, as the model header declares it. */
volatile uint32_t *tfw_mcxn_model_reg(uintptr_t addr);

#define AHBCLKCTRLSET1  0x50000224u
#define PRESETCTRLCLR1  0x50000144u
#define ADC0CLKSEL      0x500002A4u
#define ADC0CLKDIV      0x50000394u
#define ADC_CTRL        0x5010D010u
#define ADC_STAT        0x5010D014u
#define ADC_CFG         0x5010D020u
#define ADC_SWTRIG      0x5010D034u
#define ADC_TCTRL0      0x5010D0A0u
#define ADC_GCC0        0x5010D0F0u
#define ADC_GCC1        0x5010D0F4u
#define ADC_GCR0        0x5010D0F8u
#define ADC_GCR1        0x5010D0FCu
#define ADC_CMDL1       0x5010D100u
#define ADC_CMDH1       0x5010D104u
#define ADC_RESFIFO0    0x5010D300u

#define CTRL_CAL_REQ    (1u << 3)
#define STAT_CAL_RDY    (1u << 10)
#define GCC_RDY         (1u << 24)
#define GAIN_CAL        0x8000u        /* GCR = (0x8000 << 16) / 0x17FFF */

#define NREG 32u
#define NLOG 256u

static struct { uintptr_t addr; uint32_t val; } regs[NREG];
static unsigned  nregs;
static uintptr_t trace[NLOG];
static unsigned  ntrace;
static int       cal_ok;
static int       pending;
static uint32_t  pending_val;
static int       held;             /* the pending conversion outlives the poll */

static volatile uint32_t *slot(uintptr_t addr)
{
    for (unsigned i = 0u; i < nregs; i++) {
        if (regs[i].addr == addr) {
            return &regs[i].val;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(nregs < NREG, "register model full");
    regs[nregs].addr = addr;
    regs[nregs].val = 0u;
    return &regs[nregs++].val;
}

volatile uint32_t *tfw_mcxn_model_reg(uintptr_t addr)
{
    volatile uint32_t *r = slot(addr);

    if (ntrace < NLOG) {
        trace[ntrace++] = addr;
    }
    if (addr == ADC_CTRL) {
        *r &= ~(3u << 8);              /* RSTFIFO0/1 read back 0 */
    }
    if (addr == ADC_STAT && cal_ok) {
        *r |= STAT_CAL_RDY;
    }
    if ((addr == ADC_GCC0 || addr == ADC_GCC1) && cal_ok) {
        *r = GCC_RDY | GAIN_CAL;
        *slot(ADC_CTRL) &= ~CTRL_CAL_REQ;
    }
    if (addr == ADC_RESFIFO0) {
        /* A result only for a conversion software trigger 0 started. */
        if (pending && !held && (*slot(ADC_SWTRIG) & 1u) != 0u) {
            *r = (1u << 31) | (pending_val << 3);
            *slot(ADC_SWTRIG) = 0u;
            pending = 0;
        } else {
            *r = 0u;
        }
    }
    return r;
}

static uint32_t peek(uintptr_t addr)
{
    for (unsigned i = 0u; i < nregs; i++) {
        if (regs[i].addr == addr) {
            return regs[i].val;
        }
    }
    return 0u;
}

static int first(uintptr_t addr)
{
    for (unsigned i = 0u; i < ntrace; i++) {
        if (trace[i] == addr) {
            return (int)i;
        }
    }
    return -1;
}

void setUp(void)
{
    nregs = 0u;
    ntrace = 0u;
    cal_ok = 1;
    pending = 0;
    held = 0;
}

void tearDown(void) {}

static void test_init_ungates_clocks_resets_calibrates_in_order(void)
{
    static const uintptr_t order[] = {
        AHBCLKCTRLSET1, PRESETCTRLCLR1, ADC0CLKSEL, ADC0CLKDIV,
        ADC_CTRL, ADC_CFG, ADC_STAT, ADC_GCC0, ADC_GCR0, ADC_GCC1, ADC_GCR1,
    };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    for (unsigned i = 0u; i < sizeof order / sizeof order[0]; i++) {
        TEST_ASSERT_TRUE_MESSAGE(first(order[i]) >= 0, "register not touched");
        if (i > 0u) {
            TEST_ASSERT_GREATER_THAN_INT(first(order[i - 1u]), first(order[i]));
        }
    }
    TEST_ASSERT_EQUAL_HEX32(1u << 3, peek(AHBCLKCTRLSET1));
    TEST_ASSERT_EQUAL_HEX32(1u << 3, peek(PRESETCTRLCLR1));
    TEST_ASSERT_EQUAL_HEX32(3u, peek(ADC0CLKSEL));            /* FRO12M */
    TEST_ASSERT_EQUAL_HEX32(0u, peek(ADC0CLKDIV));            /* divide by 1, running */
    TEST_ASSERT_EQUAL_HEX32((1u << 28) | (0x80u << 16), peek(ADC_CFG));
    /* ADCEN, CALOFS, CAL_AVGS 128; CAL_REQ cleared by the part. */
    TEST_ASSERT_EQUAL_HEX32((7u << 16) | (1u << 4) | (1u << 0), peek(ADC_CTRL));
    TEST_ASSERT_EQUAL_HEX32((1u << 24) | 0x5555u, peek(ADC_GCR0));
    TEST_ASSERT_EQUAL_HEX32((1u << 24) | 0x5555u, peek(ADC_GCR1));
}

static void test_a_calibration_that_never_finishes_times_out(void)
{
    uint16_t raw = 0u;

    cal_ok = 0;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(14u, &raw));
}

static void test_one_read_per_header_channel(void)
{
    /* A2 B14, A3 A14, A4 B15, A5 A15 (docs/reference/README.md). */
    static const struct { uint8_t ch; uint32_t cmdl; uint16_t val; } want[] = {
        { 32u + 14u, (1u << 5) | 14u, 0x0123u },
        { 14u,       14u,             0x0FFFu },
        { 32u + 15u, (1u << 5) | 15u, 0x0000u },
        { 15u,       15u,             0x0800u },
    };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    for (unsigned i = 0u; i < sizeof want / sizeof want[0]; i++) {
        uint16_t raw = 0xFFFFu;

        pending = 1;
        pending_val = want[i].val;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(want[i].ch, &raw));
        TEST_ASSERT_EQUAL_HEX16(want[i].val, raw);
        TEST_ASSERT_EQUAL_HEX32(want[i].cmdl, peek(ADC_CMDL1));
        TEST_ASSERT_EQUAL_HEX32(7u << 8, peek(ADC_CMDH1));
        TEST_ASSERT_EQUAL_HEX32(1u << 24, peek(ADC_TCTRL0));
    }
}

static void test_an_empty_fifo_times_out(void)
{
    uint16_t raw = 0x5A5Au;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(14u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, raw);
}

static void test_a_late_result_is_not_taken_for_another_channel(void)
{
    uint16_t raw = 0x5A5Au;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    pending = 1;                       /* A14's conversion outlives the poll */
    pending_val = 0x0123u;
    held = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(14u, &raw));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(ADC_TCTRL0));
    held = 0;                          /* ...and then lands in FIFO 0 */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(15u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, raw);
}

static void test_a_channel_beyond_the_range_is_refused(void)
{
    uint16_t raw = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL_UINT32(64u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(64u, &raw));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(0xFFu, &raw));
    TEST_ASSERT_EQUAL(-1, first(ADC_SWTRIG));
}

static void test_only_the_vcc_reference_is_supported(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_ungates_clocks_resets_calibrates_in_order);
    RUN_TEST(test_a_calibration_that_never_finishes_times_out);
    RUN_TEST(test_one_read_per_header_channel);
    RUN_TEST(test_an_empty_fifo_times_out);
    RUN_TEST(test_a_late_result_is_not_taken_for_another_channel);
    RUN_TEST(test_a_channel_beyond_the_range_is_refused);
    RUN_TEST(test_only_the_vcc_reference_is_supported);
    return UNITY_END();
}
