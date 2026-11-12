/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 port's converter driver (src/hal/cortex-m/rp2350/hal_adc.c)
 * against the register model (test/model/rp2350_model.h): init, one read per
 * channel with its pad handed to the converter first, a converter that never
 * answers, a conversion that sets ERR, a channel beyond the board and the
 * references the part does not have. The board is rp2350-pico2w, three
 * channels, GPIO26-28. Built, not run on the board.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "rp2350_model.h"

#define PAD_OD     0x80u   /* PADS_BANK0 GPIOn.OD, bit 7 */
#define PAD_RESET  0x116u
#define FUNC_NULL  0x1Fu

void setUp(void)
{
    tfw_rp2350_model_reset();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp2350_model_get()->unmodelled);
}

static void test_init_clocks_the_adc_releases_it_and_waits_for_ready(void)
{
    struct tfw_rp2350_model *m;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    m = tfw_rp2350_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x800u, m->clk_adc_ctrl);    /* ENABLE, AUXSRC 0 pll_usb */
    TEST_ASSERT_EQUAL_HEX32(0x10000u, m->clk_adc_div);   /* DIV.INT 1 */
    TEST_ASSERT_EQUAL_UINT32(48000000u, tfw_rp2350_model_clk_adc_hz());
    TEST_ASSERT_EQUAL_UINT32(0u, m->auxsrc_while_enabled);
    TEST_ASSERT_EQUAL_HEX32(0u, m->reset & 1u);
    TEST_ASSERT_EQUAL_HEX32(0x1FFF7FFEu, m->reset);      /* ADC, PLL_USB; nothing else */
    TEST_ASSERT_EQUAL_UINT8(1u, m->adc.en);
    TEST_ASSERT_EQUAL_UINT8(1u, m->adc.ready);
    TEST_ASSERT_EQUAL_UINT32(0u, m->adc.conversions);
}

static void test_init_brings_pll_usb_to_48mhz_in_the_datasheet_order(void)
{
    struct tfw_rp2350_model *m;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    m = tfw_rp2350_model_get();
    TEST_ASSERT_EQUAL_UINT32(1u, m->pll.refdiv);         /* 12 MHz reference */
    TEST_ASSERT_EQUAL_UINT32(100u, m->pll.fbdiv);        /* VCO 1200 MHz */
    TEST_ASSERT_EQUAL_HEX32(0x55000u, m->pll.prim);      /* POSTDIV1 5, POSTDIV2 5 */
    TEST_ASSERT_EQUAL_HEX32(0x4u, m->pll.pwr);           /* PD, VCOPD, POSTDIVPD clear */
    TEST_ASSERT_TRUE(m->pll.lock_polls > 0u);            /* LOCK waited for */
    TEST_ASSERT_EQUAL_UINT32(0u, m->pll.out_of_order);
}

static void test_a_second_init_leaves_the_locked_pll_running(void)
{
    struct tfw_rp2350_model *m;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    m = tfw_rp2350_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->pll.out_of_order);
    TEST_ASSERT_EQUAL_UINT32(0u, m->auxsrc_while_enabled);
    TEST_ASSERT_EQUAL_UINT32(48000000u, tfw_rp2350_model_clk_adc_hz());
}

static void test_a_pll_that_never_locks_is_a_timeout_and_no_clock(void)
{
    struct tfw_rp2350_model *m;

    tfw_rp2350_model_get()->pll.never_lock = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
    m = tfw_rp2350_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x77000u, m->pll.prim);      /* post dividers untouched */
    TEST_ASSERT_EQUAL_HEX32(0u, m->clk_adc_ctrl & 0x800u);
    TEST_ASSERT_EQUAL_UINT8(0u, m->adc.en);
}

/* The 12 MHz crystal is outside §12.4.3's operating condition: the model
 * refuses to enable or convert on it. */
static void test_the_model_refuses_a_conversion_on_the_crystal(void)
{
    struct tfw_rp2350_model *m;
    uint16_t raw = 0x5555u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    m = tfw_rp2350_model_get();
    m->clk_adc_ctrl = 0x860u;                            /* ENABLE, AUXSRC 3 xosc */
    TEST_ASSERT_EQUAL_UINT32(12000000u, tfw_rp2350_model_clk_adc_hz());
    m->adc.ready = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(0u, &raw));
    m = tfw_rp2350_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->adc.conversions);
    TEST_ASSERT_TRUE(m->adc.ignored > 0u);
}

static void test_one_read_per_channel_converts_its_gpio(void)
{
    static const uint16_t levels[3] = { 0x000u, 0x7A5u, 0xFFFu };
    struct tfw_rp2350_model *m = tfw_rp2350_model_get();

    for (uint8_t ch = 0u; ch < 3u; ch++) {
        m->adc.level[ch] = levels[ch];
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    for (uint8_t ch = 0u; ch < 3u; ch++) {
        uint16_t raw = 0x5555u;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(ch, &raw));
        TEST_ASSERT_EQUAL_HEX16(levels[ch], raw);
        m = tfw_rp2350_model_get();
        TEST_ASSERT_EQUAL_UINT32(ch, m->adc.ainsel);
        TEST_ASSERT_EQUAL_UINT32(ch + 1u, m->adc.conversions);
        /* the pad when it converted: output disabled, input, pulls, ISO clear */
        TEST_ASSERT_EQUAL_HEX32(PAD_OD, m->adc.pad_at_start[ch]);
        TEST_ASSERT_EQUAL_HEX32(FUNC_NULL, m->adc.io_at_start[ch]);
    }
    TEST_ASSERT_EQUAL_HEX32(PAD_RESET, tfw_rp2350_model_get()->pad[3]);   /* GPIO29 */
}

static void test_ready_never_set_is_a_timeout(void)
{
    uint16_t raw = 0x5555u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    tfw_rp2350_model_get()->adc.stuck = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(1u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5555u, raw);
}

static void test_init_with_no_ready_is_a_timeout(void)
{
    tfw_rp2350_model_get()->adc.stuck = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_init());
}

static void test_err_set_is_an_error_not_a_value(void)
{
    struct tfw_rp2350_model *m = tfw_rp2350_model_get();
    uint16_t raw = 0x5555u;

    m->adc.level[2] = 0x123u;
    m->adc.fail = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_adc_read(2u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5555u, raw);
}

static void test_a_channel_beyond_the_board_is_refused_untouched(void)
{
    uint16_t raw = 0x5555u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(3u, &raw));    /* ADC3, GPIO29 */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(4u, &raw));    /* temperature */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(255u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5555u, raw);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp2350_model_get()->adc.conversions);
    TEST_ASSERT_EQUAL_HEX32(PAD_RESET, tfw_rp2350_model_get()->pad[3]);
}

static void test_a_read_before_init_times_out(void)
{
    uint16_t raw = 0x5555u;

    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp2350_model_get()->adc.conversions);
    TEST_ASSERT_TRUE(tfw_rp2350_model_get()->adc.ignored > 0u);
}

static void test_only_the_supply_reference_is_accepted(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref((tfw_adc_ref_t)7));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_clocks_the_adc_releases_it_and_waits_for_ready);
    RUN_TEST(test_init_brings_pll_usb_to_48mhz_in_the_datasheet_order);
    RUN_TEST(test_a_second_init_leaves_the_locked_pll_running);
    RUN_TEST(test_a_pll_that_never_locks_is_a_timeout_and_no_clock);
    RUN_TEST(test_the_model_refuses_a_conversion_on_the_crystal);
    RUN_TEST(test_one_read_per_channel_converts_its_gpio);
    RUN_TEST(test_ready_never_set_is_a_timeout);
    RUN_TEST(test_init_with_no_ready_is_a_timeout);
    RUN_TEST(test_err_set_is_an_error_not_a_value);
    RUN_TEST(test_a_channel_beyond_the_board_is_refused_untouched);
    RUN_TEST(test_a_read_before_init_times_out);
    RUN_TEST(test_only_the_supply_reference_is_accepted);
    return UNITY_END();
}
