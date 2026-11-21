/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 ADC HAL (src/hal/cortex-m/mps2/hal_adc.c), unchanged, on
 * mps2-an505's profile: init succeeds, there is no reference to select, a
 * channel at or past TFW_ADC_COUNT is out of range, any other is unsupported,
 * and a read never writes the caller's word.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"

/* Read through a variable, so a count of 0 is no always-false compare. */
static volatile uint32_t adc_count = TFW_ADC_COUNT;

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_adc_init_succeeds(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_init());
}

static void test_adc_set_ref_is_unsupported(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
}

static void test_adc_read_range_by_channel_and_out_untouched(void)
{
    for (uint32_t ch = 0u; ch <= 0xFFu; ch++) {
        uint16_t raw = 0xBEEFu;
        tfw_status_t want = ch >= adc_count ? TFW_ERR_RANGE : TFW_ERR_UNSUPPORTED;

        TEST_ASSERT_EQUAL_INT(want, tfw_hal_adc_read((uint8_t)ch, &raw));
        TEST_ASSERT_EQUAL_HEX16(0xBEEFu, raw);
    }
}

static void test_adc_board_has_no_channel(void)
{
    uint16_t raw = 0x1234u;

    TEST_ASSERT_EQUAL_UINT32(0u, adc_count);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, raw);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_adc_init_succeeds);
    RUN_TEST(test_adc_set_ref_is_unsupported);
    RUN_TEST(test_adc_read_range_by_channel_and_out_untouched);
    RUN_TEST(test_adc_board_has_no_channel);
    return UNITY_END();
}
