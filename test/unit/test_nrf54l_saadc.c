/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L15 DK's analog read (src/hal/cortex-m/nrf54l/hal_adc.c) against
 * a register model of the SAADC, on x86. The driver is the image's,
 * unchanged: this file stands in for cm_regs.h, sending TFW_CM_REG to the
 * model, and includes hal_adc.c, so the model can see the EasyDMA buffer the
 * driver points RESULT.PTR at.
 *
 * The model acts on a task at the next register access, as the i.MX 95's
 * does (test/model/imx95_model.c): START sets EVENTS_STARTED when the SAADC
 * is enabled, SAMPLE writes the next sample to RESULT.PTR and sets
 * EVENTS_END once started, STOP sets EVENTS_STOPPED. Expected register
 * values are literals from the nrfx MDK's nrf54l15_types.h.
 */

#include <stdint.h>

#include "unity.h"

#define TFW_CM_REGS_H
static volatile uint32_t *saadc_model_reg(uintptr_t addr);
#define TFW_CM_REG(addr) (*saadc_model_reg((uintptr_t)(addr)))

#include "hal_adc.c"

#define M_BASE      0x500D5000u
#define M_WORDS     (0x658u / 4u)
#define W(off)      (regs[(off) / 4u])

static uint32_t regs[M_WORDS];
static uint32_t accesses;
static uint8_t  stall_start, stall_end;
static int16_t  next_sample;
static char     seq[16];
static uint32_t nseq;
static uint32_t at_sample[6];   /* PSELP, PSELN, CONFIG, RESOLUTION, PTR, MAXCNT */
static uint8_t  dma_ok;

static void note(char c)
{
    if (nseq + 1u < sizeof(seq)) {
        seq[nseq++] = c;
        seq[nseq] = '\0';
    }
}

static void settle(void)
{
    if (W(0x000u) != 0u) {                     /* TASKS_START */
        W(0x000u) = 0u;
        note('S');
        if (W(0x500u) == 1u && !stall_start) {
            W(0x100u) = 1u;                    /* EVENTS_STARTED */
        }
    }
    if (W(0x004u) != 0u) {                     /* TASKS_SAMPLE */
        W(0x004u) = 0u;
        note('A');
        at_sample[0] = W(0x510u);
        at_sample[1] = W(0x514u);
        at_sample[2] = W(0x518u);
        at_sample[3] = W(0x5F0u);
        at_sample[4] = W(0x62Cu);
        at_sample[5] = W(0x630u);
        if (W(0x100u) != 0u && !stall_end) {
            dma_ok = (W(0x62Cu) == (uint32_t)(uintptr_t)&saadc_sample && W(0x630u) == 2u);
            if (dma_ok) {
                saadc_sample = next_sample;
            }
            W(0x104u) = 1u;                    /* EVENTS_END */
        }
    }
    if (W(0x008u) != 0u) {                     /* TASKS_STOP */
        W(0x008u) = 0u;
        note('P');
        W(0x114u) = 1u;                        /* EVENTS_STOPPED */
    }
}

static volatile uint32_t *saadc_model_reg(uintptr_t addr)
{
    accesses++;
    settle();
    TEST_ASSERT_TRUE_MESSAGE(addr >= M_BASE && addr < M_BASE + 4u * M_WORDS,
                             "an access outside the SAADC");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, (uint32_t)(addr & 3u), "an unaligned access");
    return &regs[(addr - M_BASE) / 4u];
}

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < M_WORDS; i++) {
        regs[i] = 0u;
    }
    W(0x518u) = 0x00020000u;                   /* SAADC_CH_CONFIG_ResetValue */
    accesses = 0u;
    stall_start = 0u;
    stall_end = 0u;
    next_sample = 0;
    nseq = 0u;
    seq[0] = '\0';
    dma_ok = 0u;
    saadc_sample = 0x5A5A;
}

void tearDown(void) {}

static void test_the_board_has_the_tables_four_channels(void)
{
    TEST_ASSERT_EQUAL_UINT32(4u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL_UINT32(TFW_NRF54L_SAADC_CHANNELS, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL_INT(1, TFW_FEATURE_ANALOG);
}

static void test_init_succeeds_and_touches_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL_UINT32(0u, accesses);
}

static void test_a_read_configures_starts_samples_stops_and_returns_the_result(void)
{
    uint16_t raw = 0u;

    next_sample = 1234;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_read(1u, &raw));
    settle();
    TEST_ASSERT_EQUAL_STRING("SAP", seq);
    TEST_ASSERT_TRUE(dma_ok);
    TEST_ASSERT_EQUAL_UINT16(1234u, raw);
    /* At SAMPLE: channel 1 is AIN3, P1.7, CONNECT AnalogInput. */
    TEST_ASSERT_EQUAL_HEX32(0x40000107u, at_sample[0]);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, at_sample[1]);   /* PSELN NC */
    /* GAIN Gain2_8 (7) at bit 8, REFSEL Internal, MODE SE, TACQ reset 2. */
    TEST_ASSERT_EQUAL_HEX32(0x00020700u, at_sample[2]);
    TEST_ASSERT_EQUAL_HEX32(2u, at_sample[3]);            /* RESOLUTION 12bit */
    TEST_ASSERT_EQUAL_HEX32(2u, at_sample[5]);            /* MAXCNT: one sample */
    /* Left disabled with the pin disconnected. */
    TEST_ASSERT_EQUAL_HEX32(0u, W(0x500u));
    TEST_ASSERT_EQUAL_HEX32(0u, W(0x510u));
}

static void test_each_channel_selects_its_pin(void)
{
    static const uint32_t want[4] = { 0x40000106u, 0x40000107u, 0x4000010Bu, 0x4000010Cu };
    uint8_t ch;

    for (ch = 0u; ch < 4u; ch++) {
        uint16_t raw = 0u;
        setUp();
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_read(ch, &raw));
        TEST_ASSERT_EQUAL_HEX32(want[ch], at_sample[0]);
    }
}

static void test_a_sample_below_ground_reads_zero_and_above_reads_4095(void)
{
    uint16_t raw = 77u;

    next_sample = -5;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_UINT16(0u, raw);
    setUp();
    next_sample = 4095;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_UINT16(4095u, raw);
}

static void test_an_out_of_range_channel_is_refused_before_any_register(void)
{
    uint16_t raw = 77u;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_adc_read(4u, &raw));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_adc_read(0xFFu, &raw));
    TEST_ASSERT_EQUAL_UINT16(77u, raw);
    TEST_ASSERT_EQUAL_UINT32(0u, accesses);
}

static void test_only_the_internal_reference_is_taken(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL_UINT32(0u, accesses);
}

static void test_a_saadc_that_never_starts_times_out_stopped_and_disabled(void)
{
    uint16_t raw = 77u;

    stall_start = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_adc_read(0u, &raw));
    settle();
    TEST_ASSERT_EQUAL_STRING("SP", seq);
    TEST_ASSERT_EQUAL_UINT16(77u, raw);
    TEST_ASSERT_EQUAL_HEX32(0u, W(0x500u));
    TEST_ASSERT_EQUAL_HEX32(0u, W(0x510u));
}

static void test_a_sample_that_never_ends_times_out_stopped_and_disabled(void)
{
    uint16_t raw = 77u;

    stall_end = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_adc_read(2u, &raw));
    settle();
    TEST_ASSERT_EQUAL_STRING("SAP", seq);
    TEST_ASSERT_EQUAL_UINT16(77u, raw);
    TEST_ASSERT_EQUAL_HEX32(0u, W(0x500u));
    TEST_ASSERT_EQUAL_HEX32(0u, W(0x510u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_has_the_tables_four_channels);
    RUN_TEST(test_init_succeeds_and_touches_nothing);
    RUN_TEST(test_a_read_configures_starts_samples_stops_and_returns_the_result);
    RUN_TEST(test_each_channel_selects_its_pin);
    RUN_TEST(test_a_sample_below_ground_reads_zero_and_above_reads_4095);
    RUN_TEST(test_an_out_of_range_channel_is_refused_before_any_register);
    RUN_TEST(test_only_the_internal_reference_is_taken);
    RUN_TEST(test_a_saadc_that_never_starts_times_out_stopped_and_disabled);
    RUN_TEST(test_a_sample_that_never_ends_times_out_stopped_and_disabled);
    return UNITY_END();
}
