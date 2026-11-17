/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 port's converter driver (src/hal/cortex-m/rp2350/hal_adc.c) in
 * the Pico 2 W's PERIPH=0 row, built without TFW_CM_PERIPH: no analog inputs
 * (board_profile.h). src/main.c still calls
 * tfw_hal_adc_init on every image, and it must leave PLL_USB, clk_adc, the
 * resets and the converter as the register model (adc_model.h) starts them;
 * a read is a channel beyond the board.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "adc_model.h"

_Static_assert(TFW_FEATURE_ANALOG == 0, "built as the PERIPH=0 row");

static struct tfw_rp2350_model at_reset;

void setUp(void)
{
    tfw_rp2350_model_reset();
    memcpy(&at_reset, tfw_rp2350_model_get(), sizeof at_reset);
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_rp2350_model_get()->unmodelled);
}

static void test_init_touches_no_pll_clock_reset_or_converter(void)
{
    struct tfw_rp2350_model *m;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    m = tfw_rp2350_model_get();
    TEST_ASSERT_EQUAL_HEX32(at_reset.clk_adc_ctrl, m->clk_adc_ctrl);
    TEST_ASSERT_EQUAL_HEX32(at_reset.reset, m->reset);
    TEST_ASSERT_EQUAL_HEX32(at_reset.pll.pwr, m->pll.pwr);
    TEST_ASSERT_EQUAL_UINT32(0u, m->pll.lock_polls);
    TEST_ASSERT_EQUAL_UINT8(0u, m->adc.en);
    TEST_ASSERT_EQUAL_MEMORY(&at_reset, m, sizeof at_reset);
}

static void test_a_read_is_refused_untouched(void)
{
    uint16_t raw = 0x5555u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x5555u, raw);
    TEST_ASSERT_EQUAL_MEMORY(&at_reset, tfw_rp2350_model_get(), sizeof at_reset);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_touches_no_pll_clock_reset_or_converter);
    RUN_TEST(test_a_read_is_refused_untouched);
    return UNITY_END();
}
