/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5's ADC1 driver (src/hal/cortex-m/stm32h5/hal_adc.c) against a
 * register model, as test/model/imx95_model.c models the i.MX 95: the file
 * is the image's, unchanged, with the register macro sent to the model
 * (test/model/cortex-m/stm32h5_reg_model.h claims cm_regs.h's guard first,
 * so soc_regs.h finds the model's register macro and not the bus's).
 *
 * The model applies a write when the next register access comes (or at
 * settle()), comparing the cell the driver wrote with the value it was
 * shown, so `X |= b` and `X = v` both land. It models what the driver
 * relies on: CR's set-only bits (ADCAL clears when calibration ends, ADEN
 * raises ADRDY, ADSTART converts SQR1's first channel into DR and raises
 * EOC and EOS), ISR's write-1-to-clear flags, and the ADC's bus clock gate
 * (any ADC access with ADCEN clear is an error). Each of ADCAL, ADRDY and
 * EOC can be made to hang, for the bounded waits.
 *
 * Runs under `make host-test`, against the STM32H573I-DK's board profile.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static volatile uint32_t *model_reg(uintptr_t addr);
#include "stm32h5_reg_model.h"

#include "hal_adc.c"

#include "unity.h"

enum { EV_KERCLK, EV_CLK, EV_DEEPPWD_OFF, EV_VREG, EV_CAL, EV_EN, EV_START };

static struct {
    uint32_t ccipr5, ahb2enr, ccr;
    uint32_t isr, cr, cfgr, smpr1, smpr2, sqr1, dr, difsel, or_;
    uint32_t other;                   /* any address not modelled */
    unsigned unmodelled, errors;
    uint8_t  hang_cal, hang_rdy, hang_eoc;
    uint32_t cal_dif;                 /* ADCALDIF at the calibration's start */
    uint16_t sample[TFW_H5_ADC_CHANNELS];
    uint8_t  ev[32];
    unsigned nev;
    uint8_t  conv_ch[32], conv_len[32], conv_smp[32];
    unsigned nconv;
} m;

static uint32_t          *cur;
static uint32_t           presented;
static volatile uint32_t  cell;

static void event(uint8_t e)
{
    if (m.nev < sizeof m.ev) {
        m.ev[m.nev++] = e;
    }
}

static uint32_t *decode(uintptr_t a)
{
    switch (a) {
    case TFW_H5_RCC_BASE + 0xE8u: return &m.ccipr5;
    case TFW_H5_RCC_BASE + 0x8Cu: return &m.ahb2enr;
    case 0x42028308u:             return &m.ccr;
    case TFW_H5_ADC1_BASE + 0x00u: return &m.isr;
    case TFW_H5_ADC1_BASE + 0x08u: return &m.cr;
    case TFW_H5_ADC1_BASE + 0x0Cu: return &m.cfgr;
    case TFW_H5_ADC1_BASE + 0x14u: return &m.smpr1;
    case TFW_H5_ADC1_BASE + 0x18u: return &m.smpr2;
    case TFW_H5_ADC1_BASE + 0x30u: return &m.sqr1;
    case TFW_H5_ADC1_BASE + 0x40u: return &m.dr;
    case TFW_H5_ADC1_BASE + 0xB0u: return &m.difsel;
    case TFW_H5_ADC1_BASE + 0xC8u: return &m.or_;
    default:
        m.unmodelled++;
        return &m.other;
    }
}

static uint8_t adc_reg(const uint32_t *r)
{
    return r != &m.ccipr5 && r != &m.ahb2enr && r != &m.other;
}

static void convert(void)
{
    uint8_t ch  = (uint8_t)((m.sqr1 & TFW_H5_ADC_SQR1_SQ1) >> TFW_H5_ADC_SQR1_SQ1_POS);
    uint32_t sm = (ch < 10u) ? m.smpr1 : m.smpr2;

    if (m.nconv < sizeof m.conv_ch) {
        m.conv_ch[m.nconv]  = ch;
        m.conv_len[m.nconv] = (uint8_t)(m.sqr1 & TFW_H5_ADC_SQR1_L);
        m.conv_smp[m.nconv] = (uint8_t)((sm >> (3u * (ch % 10u))) & 0x7u);
        m.nconv++;
    }
    if (m.hang_eoc) {
        m.cr |= TFW_H5_ADC_CR_ADSTART;
        return;
    }
    m.dr   = (ch < TFW_H5_ADC_CHANNELS) ? m.sample[ch] : 0u;
    m.isr |= TFW_H5_ADC_ISR_EOC | TFW_H5_ADC_ISR_EOS;
}

static void write_cr(uint32_t v, uint32_t old)
{
    uint32_t keep = TFW_H5_ADC_CR_DEEPPWD | TFW_H5_ADC_CR_ADVREGEN | TFW_H5_ADC_CR_ADCALDIF;

    m.cr = (m.cr & ~keep) | (v & keep);
    if ((old & TFW_H5_ADC_CR_DEEPPWD) && !(m.cr & TFW_H5_ADC_CR_DEEPPWD)) {
        event(EV_DEEPPWD_OFF);
    }
    if (!(old & TFW_H5_ADC_CR_ADVREGEN) && (m.cr & TFW_H5_ADC_CR_ADVREGEN)) {
        if (m.cr & TFW_H5_ADC_CR_DEEPPWD) {
            m.errors++;                           /* the regulator in deep power-down */
        }
        event(EV_VREG);
    }
    if (v & TFW_H5_ADC_CR_ADCAL) {
        event(EV_CAL);
        if ((m.cr & TFW_H5_ADC_CR_ADEN) || !(m.cr & TFW_H5_ADC_CR_ADVREGEN)) {
            m.errors++;                           /* calibrating enabled, or unpowered */
        }
        m.cal_dif = v & TFW_H5_ADC_CR_ADCALDIF;
        if (m.hang_cal) {
            m.cr |= TFW_H5_ADC_CR_ADCAL;
        }
    }
    if (v & TFW_H5_ADC_CR_ADEN) {
        event(EV_EN);
        if (m.cr & TFW_H5_ADC_CR_ADCAL) {
            m.errors++;
        }
        m.cr |= TFW_H5_ADC_CR_ADEN;
        if (!m.hang_rdy) {
            m.isr |= TFW_H5_ADC_ISR_ADRDY;
        }
    }
    if (v & TFW_H5_ADC_CR_ADSTART) {
        event(EV_START);
        if (!(m.isr & TFW_H5_ADC_ISR_ADRDY)) {
            m.errors++;                           /* a start before the ADC is ready */
        }
        convert();
    }
}

static void on_write(uint32_t *r, uint32_t v, uint32_t old)
{
    if (r == &m.ahb2enr && !(old & TFW_H5_RCC_AHB2ENR_ADCEN) && (v & TFW_H5_RCC_AHB2ENR_ADCEN)) {
        event(EV_CLK);
    }
    if (r == &m.ccipr5) {
        event(EV_KERCLK);
    }
    if (r == &m.ccr && (m.cr & TFW_H5_ADC_CR_ADEN)) {
        m.errors++;                               /* the clock changed under an enabled ADC */
    }
    if (r == &m.cr) {
        write_cr(v, old);
    } else if (r == &m.isr) {
        m.isr &= ~v;
    } else {
        *r = v;
    }
}

static void settle(void)
{
    if (cur != NULL && cell != presented) {
        uint32_t old = presented;

        presented = cell;
        on_write(cur, cell, old);
    }
    cur = NULL;
}

static volatile uint32_t *model_reg(uintptr_t addr)
{
    settle();
    cur = decode(addr);
    if (adc_reg(cur) && !(m.ahb2enr & TFW_H5_RCC_AHB2ENR_ADCEN)) {
        m.errors++;
    }
    presented = *cur;
    cell      = presented;
    return &cell;
}

void setUp(void)
{
    memset(&m, 0, sizeof m);
    m.cr   = TFW_H5_ADC_CR_DEEPPWD;                 /* the reset values */
    m.cfgr = 0x80000000u;
    m.ccipr5 = 0x1u;              /* SYSCLK, so selecting HCLK is a write the model sees */
    cur    = NULL;
    ready  = 0u;
}

void tearDown(void) {}

static void test_init_brings_the_adc_up_in_order(void)
{
    static const uint8_t want[] = { EV_KERCLK, EV_CLK, EV_DEEPPWD_OFF, EV_VREG, EV_CAL, EV_EN };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    settle();
    TEST_ASSERT_EQUAL_UINT(sizeof want, m.nev);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, m.ev, sizeof want);
    TEST_ASSERT_EQUAL_UINT(0u, m.errors);
    TEST_ASSERT_EQUAL_UINT(0u, m.unmodelled);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_RCC_ADCDACSEL_HCLK, m.ccipr5 & TFW_H5_RCC_CCIPR5_ADCDACSEL);
    TEST_ASSERT_EQUAL_HEX32(0u, m.ccr & TFW_H5_ADC_CCR_CKMODE);
    TEST_ASSERT_EQUAL_HEX32(1u << 18, m.ccr & TFW_H5_ADC_CCR_PRESC);
    TEST_ASSERT_EQUAL_HEX32(0u, m.cal_dif);                         /* single-ended */
    TEST_ASSERT_EQUAL_HEX32(0u, m.difsel);
    TEST_ASSERT_TRUE(m.or_ & TFW_H5_ADC_OR_OP0);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_ADC_CR_ADVREGEN | TFW_H5_ADC_CR_ADEN, m.cr);
    TEST_ASSERT_EQUAL_HEX32(0u, m.cfgr & (TFW_H5_ADC_CFGR_RES | TFW_H5_ADC_CFGR_ALIGN |
                                          TFW_H5_ADC_CFGR_CONT));
    TEST_ASSERT_TRUE(m.cfgr & TFW_H5_ADC_CFGR_OVRMOD);
    TEST_ASSERT_EQUAL_UINT(0u, m.nconv);
}

static void test_each_channel_is_one_conversion_of_its_12_bits(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    for (uint8_t ch = 0u; ch < TFW_ADC_COUNT; ch++) {
        m.sample[ch] = (uint16_t)(0xF000u | (100u * ch + 7u));    /* the top nibble is not data */
    }
    for (uint8_t ch = 0u; ch < TFW_ADC_COUNT; ch++) {
        uint16_t raw = 0xFFFFu;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(ch, &raw));
        settle();
        TEST_ASSERT_EQUAL_HEX16((uint16_t)(100u * ch + 7u), raw);
        TEST_ASSERT_EQUAL_UINT(ch + 1u, m.nconv);
        TEST_ASSERT_EQUAL_UINT8(ch, m.conv_ch[ch]);
        TEST_ASSERT_EQUAL_UINT8(0u, m.conv_len[ch]);                /* L 0: one conversion */
        TEST_ASSERT_EQUAL_UINT8(TFW_H5_ADC_SMP_640C5, m.conv_smp[ch]);
    }
    TEST_ASSERT_EQUAL_UINT(0u, m.errors);
    TEST_ASSERT_EQUAL_UINT(0u, m.unmodelled);
}

static void test_a_calibration_that_never_ends_is_a_timeout(void)
{
    uint16_t raw;

    m.hang_cal = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
    settle();
    TEST_ASSERT_FALSE(m.cr & TFW_H5_ADC_CR_ADEN);                   /* never enabled */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(9u, &raw));
    settle();
    TEST_ASSERT_EQUAL_UINT(0u, m.nconv);
}

static void test_adrdy_never_set_is_a_timeout(void)
{
    uint16_t raw;

    m.hang_rdy = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(9u, &raw));
    settle();
    TEST_ASSERT_EQUAL_UINT(0u, m.nconv);
}

static void test_eoc_never_set_is_a_timeout(void)
{
    uint16_t raw = 0x1234u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    m.hang_eoc = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(18u, &raw));
    settle();
    TEST_ASSERT_EQUAL_HEX16(0x1234u, raw);                          /* not written */
    TEST_ASSERT_EQUAL_UINT(1u, m.nconv);
}

static void test_a_channel_past_the_board_is_refused_before_any_access(void)
{
    static const uint8_t bad[] = { TFW_ADC_COUNT, TFW_ADC_COUNT + 1u, 0x80u, 0xFFu };
    uint16_t raw;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    settle();
    for (unsigned i = 0u; i < sizeof bad; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(bad[i], &raw));
    }
    settle();
    TEST_ASSERT_EQUAL_UINT(0u, m.nconv);
}

static void test_vref_plus_is_the_one_reference(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
}

static void test_the_arithmetic(void)
{
    /* Channel 9 is SMPR1's top field (bits 29:27), 10 SMPR2's first. */
    TEST_ASSERT_EQUAL_HEX32(0x38000000u, tfw_h5_adc_smpr(0u, 9u, 7u));
    TEST_ASSERT_EQUAL_HEX32(0x00000007u, tfw_h5_adc_smpr(0u, 10u, 7u));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFDu, tfw_h5_adc_smpr(0xFFFFFFFFu, 0u, 5u));
    TEST_ASSERT_EQUAL_HEX32(0x00000E00u, tfw_h5_adc_smpr(0u, 13u, 7u));
    TEST_ASSERT_EQUAL_UINT32(1280u, tfw_h5_spins_us(64000000uL, 20u));
    TEST_ASSERT_EQUAL_UINT32(8u, tfw_h5_adc_cal_en_spins());
    TEST_ASSERT_EQUAL_UINT32(20u, TFW_ADC_COUNT);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_brings_the_adc_up_in_order);
    RUN_TEST(test_each_channel_is_one_conversion_of_its_12_bits);
    RUN_TEST(test_a_calibration_that_never_ends_is_a_timeout);
    RUN_TEST(test_adrdy_never_set_is_a_timeout);
    RUN_TEST(test_eoc_never_set_is_a_timeout);
    RUN_TEST(test_a_channel_past_the_board_is_refused_before_any_access);
    RUN_TEST(test_vref_plus_is_the_one_reference);
    RUN_TEST(test_the_arithmetic);
    return UNITY_END();
}
