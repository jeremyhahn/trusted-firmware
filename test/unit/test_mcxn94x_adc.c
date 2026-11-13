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

#define CLKDIV_RESET    (1u << 29)
#define CLKDIV_HALT     (1u << 30)
#define CTRL_ADCEN      (1u << 0)
#define CTRL_RST        (1u << 1)
#define CTRL_CAL_REQ    (1u << 3)
#define CTRL_CALOFS     (1u << 4)
#define CTRL_RSTFIFO0   (1u << 8)
#define CTRL_RSTFIFO1   (1u << 9)
#define STAT_CAL_RDY    (1u << 10)
#define CFG_PWREN       (1u << 28)
#define GCC_RDY         (1u << 24)
#define GCR_RDY         (1u << 24)
#define GAIN_CAL        0x8000u        /* GCR = (0x8000 << 16) / 0x17FFF */
#define RES_VALID       (1u << 31)

#define NREG 32u
#define NLOG 256u
#define NFIFO 4u

/*
 * Each register holds the value the code last saw and the value it holds
 * now. The model hands out a pointer and cannot see the store through it,
 * so it compares the two at the next access: a difference is a write,
 * logged with its value and applied to the part's state. A store of the
 * value already there is not seen, and none of the init's stores is one.
 */
static struct { uintptr_t addr; uint32_t val; uint32_t seen; } regs[NREG];
static unsigned  nregs;
static int       last;                 /* the register the last access returned */
static struct { uintptr_t addr; uint32_t val; } wlog[NLOG];
static unsigned  nwlog;
static uintptr_t trace[NLOG];
static unsigned  ntrace;

/* The part: each step takes effect only after the ones before it. */
static int       gated, released, div_reset, div_ok;
static int       conv_reset, conv_out, fifo0_reset, fifo1_reset, powered;
static int       enabled, ofs_done, gain_req, gain_done;
static int       ofs_ok, gain_ok;      /* the calibrations finish */
static int       held;                 /* a conversion outlives the poll */
static int       late;                 /* ... and is still in flight */
static uint32_t  late_val;
static uint32_t  chan_val[64];         /* each input's result */
static uint32_t  fifo[NFIFO];
static unsigned  nfifo;

static int slot(uintptr_t addr)
{
    for (unsigned i = 0u; i < nregs; i++) {
        if (regs[i].addr == addr) {
            return (int)i;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(nregs < NREG, "register model full");
    regs[nregs].addr = addr;
    regs[nregs].val = 0u;
    regs[nregs].seen = 0u;
    return (int)nregs++;
}

/* A change the part makes, not a write. */
static void set(uintptr_t addr, uint32_t v)
{
    int i = slot(addr);

    regs[i].val = v;
    regs[i].seen = v;
}

static uint32_t peek(uintptr_t addr);

static void on_write(uintptr_t addr, uint32_t v)
{
    if (addr == AHBCLKCTRLSET1 && (v & (1u << 3)) != 0u) {
        gated = 1;
    } else if (addr == PRESETCTRLCLR1 && (v & (1u << 3)) != 0u) {
        released = gated;
    } else if (addr == ADC0CLKDIV) {
        if (v == (CLKDIV_RESET | CLKDIV_HALT)) {
            div_reset = released;
        } else if (v == 0u) {
            div_ok = div_reset;
        }
    } else if (addr == ADC_CFG) {
        powered = conv_out && (v & CFG_PWREN) != 0u;
    } else if (addr == ADC_CTRL) {
        if ((v & CTRL_RST) != 0u) {
            conv_reset = div_ok;
            conv_out = enabled = ofs_done = gain_req = gain_done = 0;
            nfifo = 0u;
        } else if (conv_reset) {
            conv_out = 1;
        }
        if ((v & CTRL_RSTFIFO0) != 0u) {
            fifo0_reset = conv_out;
            nfifo = 0u;
        }
        if ((v & CTRL_RSTFIFO1) != 0u) {
            fifo1_reset = conv_out;
        }
        v &= ~(CTRL_RSTFIFO0 | CTRL_RSTFIFO1);     /* they read back 0 */
        if ((v & CTRL_ADCEN) != 0u && !enabled) {
            enabled = powered && fifo0_reset && fifo1_reset;
        }
        if ((v & CTRL_CALOFS) != 0u && enabled && !ofs_done) {
            ofs_done = ofs_ok;
        }
        if ((v & CTRL_CAL_REQ) != 0u && ofs_done && !gain_req) {
            gain_req = 1;
            gain_done = gain_ok;
        }
        if (gain_done) {
            v &= ~CTRL_CAL_REQ;                     /* the part clears it */
        }
        set(ADC_CTRL, v);
    } else if (addr == ADC_SWTRIG && (v & 1u) != 0u) {
        if (enabled && gain_done && peek(ADC_TCTRL0) == (1u << 24)) {
            uint32_t r = chan_val[peek(ADC_CMDL1) & 0x3Fu];

            if (held) {
                late = 1;
                late_val = r;
            } else if (nfifo < NFIFO) {
                fifo[nfifo++] = r;
            }
        }
        set(ADC_SWTRIG, 0u);                       /* self-clearing */
    }
}

static void settle(void)
{
    if (last >= 0 && regs[last].val != regs[last].seen) {
        TEST_ASSERT_TRUE_MESSAGE(nwlog < NLOG, "write log full");
        wlog[nwlog].addr = regs[last].addr;
        wlog[nwlog].val = regs[last].val;
        nwlog++;
        regs[last].seen = regs[last].val;
        on_write(regs[last].addr, regs[last].val);
    }
    last = -1;
}

volatile uint32_t *tfw_mcxn_model_reg(uintptr_t addr)
{
    settle();
    if (ntrace < NLOG) {
        trace[ntrace++] = addr;
    }
    if (addr == ADC_STAT) {
        set(addr, ofs_done && (!gain_req || gain_done) ? STAT_CAL_RDY : 0u);
    }
    if (addr == ADC_GCC0 || addr == ADC_GCC1) {
        set(addr, gain_done ? (GCC_RDY | GAIN_CAL) : 0u);
    }
    if (addr == ADC_RESFIFO0) {
        if (nfifo > 0u) {
            set(addr, RES_VALID | (fifo[0] << 3));
            for (unsigned i = 1u; i < nfifo; i++) {
                fifo[i - 1u] = fifo[i];
            }
            nfifo--;
        } else {
            set(addr, 0u);
        }
    }
    last = slot(addr);
    return &regs[last].val;
}

/* The held conversion finishes after the read gave up on it. */
static void land(void)
{
    if (late && nfifo < NFIFO) {
        fifo[nfifo++] = late_val;
    }
    late = 0;
    held = 0;
}

static uint32_t peek(uintptr_t addr)
{
    settle();
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

/* Whether any write to addr set any of bits. */
static int written(uintptr_t addr, uint32_t bits)
{
    settle();
    for (unsigned i = 0u; i < nwlog; i++) {
        if (wlog[i].addr == addr && (wlog[i].val & bits) != 0u) {
            return 1;
        }
    }
    return 0;
}

void setUp(void)
{
    nregs = 0u;
    last = -1;
    nwlog = 0u;
    ntrace = 0u;
    gated = released = div_reset = div_ok = 0;
    conv_reset = conv_out = fifo0_reset = fifo1_reset = powered = 0;
    enabled = ofs_done = gain_req = gain_done = 0;
    ofs_ok = gain_ok = 1;
    held = late = 0;
    nfifo = 0u;
    for (unsigned i = 0u; i < 64u; i++) {
        chan_val[i] = 0x0100u + i;
    }
}

void tearDown(void) {}

static void test_init_ungates_clocks_resets_calibrates_in_order(void)
{
    /* fsl_lpadc's LPADC_Init, LPADC_DoOffsetCalibration and
     * LPADC_DoAutoCalibration, as docs/reference/README.md pins them. */
    static const struct { uintptr_t addr; uint32_t val; } want[] = {
        { AHBCLKCTRLSET1, 1u << 3 },
        { PRESETCTRLCLR1, 1u << 3 },
        { ADC0CLKSEL,     3u },                            /* FRO12M */
        { ADC0CLKDIV,     CLKDIV_RESET | CLKDIV_HALT },
        { ADC0CLKDIV,     0u },                            /* divide by 1, running */
        { ADC_CTRL,       CTRL_RST },
        { ADC_CTRL,       0u },
        { ADC_CTRL,       CTRL_RSTFIFO0 | CTRL_RSTFIFO1 },
        { ADC_CFG,        CFG_PWREN | (0x80u << 16) },
        { ADC_CTRL,       CTRL_ADCEN },
        { ADC_CTRL,       CTRL_CALOFS | CTRL_ADCEN },
        { ADC_CTRL,       (7u << 16) | CTRL_CALOFS | CTRL_ADCEN },
        { ADC_CTRL,       (7u << 16) | CTRL_CAL_REQ | CTRL_CALOFS | CTRL_ADCEN },
        { ADC_GCR0,       0x5555u },
        { ADC_GCR0,       GCR_RDY | 0x5555u },
        { ADC_GCR1,       0x5555u },
        { ADC_GCR1,       GCR_RDY | 0x5555u },
    };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    settle();
    TEST_ASSERT_EQUAL_UINT(sizeof want / sizeof want[0], nwlog);
    for (unsigned i = 0u; i < nwlog; i++) {
        TEST_ASSERT_EQUAL_HEX32(want[i].addr, wlog[i].addr);
        TEST_ASSERT_EQUAL_HEX32(want[i].val, wlog[i].val);
    }
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_TRUE(ofs_done);
    TEST_ASSERT_TRUE(gain_done);
    /* CAL_REQ cleared by the part. */
    TEST_ASSERT_EQUAL_HEX32((7u << 16) | CTRL_CALOFS | CTRL_ADCEN, peek(ADC_CTRL));
}

static void test_an_offset_calibration_that_never_finishes_times_out(void)
{
    uint16_t raw = 0u;

    ofs_ok = 0;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
    TEST_ASSERT_TRUE(written(ADC_CTRL, CTRL_CALOFS));
    TEST_ASSERT_FALSE(written(ADC_CTRL, CTRL_CAL_REQ));     /* gain not started */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(14u, &raw));
}

static void test_a_gain_calibration_that_never_finishes_times_out(void)
{
    uint16_t raw = 0u;

    gain_ok = 0;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
    TEST_ASSERT_TRUE(ofs_done);
    TEST_ASSERT_TRUE(written(ADC_CTRL, CTRL_CAL_REQ));
    TEST_ASSERT_FALSE(written(ADC_GCR0, GCR_RDY));          /* no gain result */
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

    for (unsigned i = 0u; i < sizeof want / sizeof want[0]; i++) {
        chan_val[want[i].cmdl] = want[i].val;
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    for (unsigned i = 0u; i < sizeof want / sizeof want[0]; i++) {
        uint16_t raw = 0xFFFFu;

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
    held = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(14u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, raw);
}

static void test_a_late_result_is_not_taken_for_another_channel(void)
{
    uint16_t raw = 0x5A5Au;

    chan_val[14] = 0x0123u;
    chan_val[15] = 0x0456u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    held = 1;                          /* A14's conversion outlives the poll */
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(14u, &raw));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(ADC_TCTRL0));
    TEST_ASSERT_EQUAL_HEX32(ADC_CTRL, wlog[nwlog - 1u].addr);   /* FIFO 0 dropped last */
    TEST_ASSERT_EQUAL_HEX32(CTRL_RSTFIFO0, wlog[nwlog - 1u].val & (CTRL_RSTFIFO0 | CTRL_RSTFIFO1));
    land();                            /* ...and then lands in FIFO 0 */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(15u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5A5Au, raw);

    /* Init again resets the converter and its FIFOs: A15 reads its own. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(15u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x0456u, raw);
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
    RUN_TEST(test_an_offset_calibration_that_never_finishes_times_out);
    RUN_TEST(test_a_gain_calibration_that_never_finishes_times_out);
    RUN_TEST(test_one_read_per_header_channel);
    RUN_TEST(test_an_empty_fifo_times_out);
    RUN_TEST(test_a_late_result_is_not_taken_for_another_channel);
    RUN_TEST(test_a_channel_beyond_the_range_is_refused);
    RUN_TEST(test_only_the_vcc_reference_is_supported);
    return UNITY_END();
}
