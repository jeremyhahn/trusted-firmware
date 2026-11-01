/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 pin HALs on the host (#163), linked together against the gpio,
 * LEDC and oneshot ADC drivers in test/mock/esp_pins_mock.c. Each HAL's
 * branches are pinned by its own test_esp_*_mutate.c; what stays here is
 * what none of those asserts: the board's last ADC channel is ADC_CHANNEL_7.
 */
#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "esp_pins_mock.h"

void setUp(void)
{
    (void)tfw_hal_adc_set_ref(TFW_ADC_REF_VCC);
    mock_pins_reset();
}

void tearDown(void)
{
}

/* --- hal_adc.c -------------------------------------------------------- */

static void test_the_last_adc_channel_is_read(void)
{
    uint16_t raw = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read((uint8_t)(TFW_ADC_COUNT - 1u), &raw));
    TEST_ASSERT_EQUAL_INT(ADC_CHANNEL_7, mock_pins.read_channel);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_last_adc_channel_is_read);
    return UNITY_END();
}
