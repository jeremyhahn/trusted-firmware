/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 ADC HAL on a part whose ADC cannot be brought up (#163):
 * src/hal/esp32/hal_adc.c against test/mock/esp_pins_mock.c, with the first
 * adc_oneshot_new_unit failing. hal_adc.c remembers that for the life of the
 * program, so it has a program of its own; test_esp_pins.c reads a unit that
 * came up.
 */
#include "unity.h"
#include "tfw/hal.h"
#include "esp_pins_mock.h"

void setUp(void)
{
    mock_pins_reset();
}

void tearDown(void)
{
}

static void test_an_adc_unit_that_fails_to_come_up_answers_unsupported(void)
{
    uint16_t raw = 0xEEEEu;

    mock_pins.new_unit_err = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_UINT16(0xEEEEu, raw);
    TEST_ASSERT_EQUAL_UINT(1u, mock_pins.new_units);
    TEST_ASSERT_EQUAL_INT(ADC_UNIT_1, mock_pins.unit_id);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.adc_configs);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.adc_reads);
}

static void test_a_failed_adc_unit_is_not_tried_again(void)
{
    uint16_t raw = 0xEEEEu;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(0u, &raw));
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.new_units);
    TEST_ASSERT_EQUAL_UINT(0u, mock_pins.adc_configs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_adc_unit_that_fails_to_come_up_answers_unsupported);
    RUN_TEST(test_a_failed_adc_unit_is_not_tried_again);
    return UNITY_END();
}
