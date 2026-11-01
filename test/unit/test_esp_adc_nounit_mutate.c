/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 analog read, src/hal/esp32/hal_adc.c,
 * on a part whose ADC does not come up (Espressif's QEMU models none).
 *
 * The other outcome is test_esp_adc_mutate.c; the unit's state is static, so
 * each outcome of the bring-up is its own binary. This holds:
 *
 *   - a channel past TFW_ADC_COUNT refused without trying the bring-up;
 *   - a failed bring-up unsupported, no channel configured or read;
 *   - the bring-up tried once: every later read unsupported without it.
 */

#include <stdint.h>
#include "unity.h"
#include "esp_adc/adc_oneshot.h"
#include "tfw/hal.h"
#include "tfw/config.h"

static unsigned n_new, n_other;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config,
                               adc_oneshot_unit_handle_t *ret_unit)
{
    (void)init_config;
    (void)ret_unit;
    n_new++;
    return ESP_FAIL;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *config)
{
    (void)handle;
    (void)channel;
    (void)config;
    n_other++;
    return ESP_OK;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw)
{
    (void)handle;
    (void)chan;
    *out_raw = 1;
    n_other++;
    return ESP_OK;
}

void setUp(void)
{
    n_new = n_other = 0u;
}

void tearDown(void) {}

static void test_a_failed_bring_up_is_unsupported(void)
{
    uint16_t out = 0xBEEFu;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read(0u, &out));
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, out);
    TEST_ASSERT_EQUAL_UINT(1u, n_new);
    TEST_ASSERT_EQUAL_UINT(0u, n_other);
}

static void test_the_bring_up_is_tried_once(void)
{
    uint16_t out = 0xBEEFu;
    unsigned i;

    for (i = 0u; i < 3u; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_read((uint8_t)i, &out));
    }
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, out);
    TEST_ASSERT_EQUAL_UINT(0u, n_new);
    TEST_ASSERT_EQUAL_UINT(0u, n_other);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_bring_up_is_unsupported);
    RUN_TEST(test_the_bring_up_is_tried_once);
    return UNITY_END();
}
