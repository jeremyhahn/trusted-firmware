/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The image's MPS2 ADC (src/hal/cortex-m/mps2/hal_adc.c) on the host, under
 * mps2-an505's profile, linked with the bus register model
 * (test/model/mps2_bus_model.h). Neither profile wires a channel
 * (TFW_ADC_COUNT 0): every read is out of range and leaves the caller's word
 * alone, and no call touches a register. The model fails any register access
 * made outside the bus accessors, and every hook below fails too, so an ADC
 * that reached for a register would fail here.
 */
#include <stdint.h>
#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "mps2_bus_model.h"

uint32_t tfw_mps2_model_sb_lines(void)
{
    TEST_FAIL_MESSAGE("the ADC read the SBCon");
    return 0u;
}
void tfw_mps2_model_sb_release(uint32_t mask)
{
    (void)mask;
    TEST_FAIL_MESSAGE("the ADC wrote the SBCon");
}
void tfw_mps2_model_sb_drive_low(uint32_t mask)
{
    (void)mask;
    TEST_FAIL_MESSAGE("the ADC wrote the SBCon");
}
uint32_t tfw_mps2_model_ssp_read(uint32_t off)
{
    (void)off;
    TEST_FAIL_MESSAGE("the ADC read the PL022");
    return 0u;
}
void tfw_mps2_model_ssp_write(uint32_t off, uint32_t v)
{
    (void)off;
    (void)v;
    TEST_FAIL_MESSAGE("the ADC wrote the PL022");
}
uint32_t tfw_mps2_model_misc_read(void)
{
    TEST_FAIL_MESSAGE("the ADC read MISC");
    return 0u;
}
void tfw_mps2_model_misc_write(uint32_t v)
{
    (void)v;
    TEST_FAIL_MESSAGE("the ADC wrote MISC");
}

void setUp(void) {}
void tearDown(void) {}

static void test_profile_has_no_channel(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)TFW_ADC_COUNT);
}

static void test_init_succeeds(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_init());
}

/* Refusal: no reference can be selected, whichever is asked for. */
static void test_set_ref_is_unsupported(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
}

/* Refusal: every channel, 0 and 255 included, is out of range, and the
 * caller's word is not written. */
static void test_read_refuses_every_channel_and_leaves_the_word(void)
{
    uint32_t ch;
    uint16_t raw;

    for (ch = 0u; ch <= 0xFFu; ch++) {
        raw = 0xBEEFu;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_adc_read((uint8_t)ch, &raw));
        TEST_ASSERT_EQUAL_HEX16(0xBEEFu, raw);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_profile_has_no_channel);
    RUN_TEST(test_init_succeeds);
    RUN_TEST(test_set_ref_is_unsupported);
    RUN_TEST(test_read_refuses_every_channel_and_leaves_the_word);
    return UNITY_END();
}
