/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 analog read, src/hal/esp32/hal_adc.c,
 * on a part whose ADC comes up.
 *
 * The unit had no host build. This builds it over a stand-in for ESP-IDF's
 * oneshot ADC driver (test/mock/esp32/adc/), each call logged here and able
 * to fail, and holds:
 *
 *   - a channel past TFW_ADC_COUNT refused before any call is made;
 *   - ADC unit 1 created on the first read, once, and its handle used;
 *   - the channel configured at the reference's attenuation (12 dB until a
 *     reference is set, 12 dB for VCC, 0 dB for INTERNAL) and the default
 *     width, an external reference refused and the attenuation kept;
 *   - a channel the driver will not configure out of range, a failed read a
 *     timeout, neither writing the result;
 *   - the converter's raw value returned whole.
 *
 * A part whose ADC does not come up is test_esp_adc_nounit_mutate.c: the
 * unit's state is static, so each outcome of the bring-up is its own binary.
 */

#include <stdint.h>
#include "unity.h"
#include "esp_adc/adc_oneshot.h"
#include "tfw/hal.h"
#include "tfw/config.h"

static struct adc_oneshot_unit_ctx_t *const the_unit =
    (struct adc_oneshot_unit_ctx_t *)(uintptr_t)0x5A5A0;

static unsigned    n_new, n_cfg, n_read;
static adc_unit_t  new_unit_id;
static adc_oneshot_unit_handle_t cfg_handle, read_handle;
static int         cfg_chan, read_chan;
static adc_oneshot_chan_cfg_t cfg_seen;
static esp_err_t   cfg_err, read_err;
static int         raw_in;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config,
                               adc_oneshot_unit_handle_t *ret_unit)
{
    n_new++;
    new_unit_id = init_config->unit_id;
    *ret_unit   = the_unit;
    return ESP_OK;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *config)
{
    n_cfg++;
    cfg_handle = handle;
    cfg_chan   = channel;
    cfg_seen   = *config;
    return cfg_err;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw)
{
    n_read++;
    read_handle = handle;
    read_chan   = chan;
    *out_raw    = raw_in;
    return read_err;
}

void setUp(void)
{
    n_new = n_cfg = n_read = 0u;
    new_unit_id = ADC_UNIT_2;
    cfg_handle = read_handle = NULL;
    cfg_chan = read_chan = -1;
    cfg_seen.atten    = (adc_atten_t)0x7F;
    cfg_seen.bitwidth = (adc_bitwidth_t)0x7F;
    cfg_err = read_err = ESP_OK;
    raw_in  = 0;
}

void tearDown(void) {}

static void test_init_does_nothing_and_succeeds(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL_UINT(0u, n_new + n_cfg + n_read);
}

static void test_a_channel_past_the_count_is_refused_untouched(void)
{
    static const uint8_t bad[] = { TFW_ADC_COUNT, TFW_ADC_COUNT + 1u, 0x80u, 0xFFu };
    unsigned i;

    for (i = 0u; i < sizeof bad; i++) {
        uint16_t out = 0xBEEFu;

        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(bad[i], &out));
        TEST_ASSERT_EQUAL_HEX16(0xBEEFu, out);
    }
    TEST_ASSERT_EQUAL_UINT(0u, n_new + n_cfg + n_read);
}

/* Runs before any reference is set: the attenuation starts at 12 dB. */
static void test_the_first_read_makes_unit_1_at_12_db(void)
{
    uint16_t out = 0u;

    raw_in = 1234;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(3u, &out));
    TEST_ASSERT_EQUAL_UINT16(1234u, out);
    TEST_ASSERT_EQUAL_UINT(1u, n_new);
    TEST_ASSERT_EQUAL(ADC_UNIT_1, new_unit_id);
    TEST_ASSERT_EQUAL_UINT(1u, n_cfg);
    TEST_ASSERT_EQUAL_PTR(the_unit, cfg_handle);
    TEST_ASSERT_EQUAL_INT(3, cfg_chan);
    TEST_ASSERT_EQUAL(ADC_ATTEN_DB_12, cfg_seen.atten);
    TEST_ASSERT_EQUAL(ADC_BITWIDTH_DEFAULT, cfg_seen.bitwidth);
    TEST_ASSERT_EQUAL_UINT(1u, n_read);
    TEST_ASSERT_EQUAL_PTR(the_unit, read_handle);
    TEST_ASSERT_EQUAL_INT(3, read_chan);
}

static void test_the_unit_is_made_once(void)
{
    uint16_t out = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(0u, &out));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(TFW_ADC_COUNT - 1u, &out));
    TEST_ASSERT_EQUAL_UINT(0u, n_new);
    TEST_ASSERT_EQUAL_UINT(2u, n_cfg);
    TEST_ASSERT_EQUAL_UINT(2u, n_read);
    TEST_ASSERT_EQUAL_INT((int)TFW_ADC_COUNT - 1, cfg_chan);
    TEST_ASSERT_EQUAL_INT((int)TFW_ADC_COUNT - 1, read_chan);
    TEST_ASSERT_EQUAL_PTR(the_unit, read_handle);
}

static void test_the_reference_is_the_attenuation(void)
{
    uint16_t out = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(1u, &out));
    TEST_ASSERT_EQUAL(ADC_ATTEN_DB_0, cfg_seen.atten);

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(1u, &out));
    TEST_ASSERT_EQUAL(ADC_ATTEN_DB_0, cfg_seen.atten);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(1u, &out));
    TEST_ASSERT_EQUAL(ADC_ATTEN_DB_12, cfg_seen.atten);

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref((tfw_adc_ref_t)3));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(1u, &out));
    TEST_ASSERT_EQUAL(ADC_ATTEN_DB_12, cfg_seen.atten);
    TEST_ASSERT_EQUAL(ADC_BITWIDTH_DEFAULT, cfg_seen.bitwidth);
}

static void test_a_channel_the_driver_refuses_is_out_of_range(void)
{
    static const esp_err_t errs[] = { ESP_ERR_INVALID_ARG, ESP_FAIL };
    unsigned i;

    for (i = 0u; i < sizeof errs / sizeof errs[0]; i++) {
        uint16_t out = 0xBEEFu;

        n_read  = 0u;
        cfg_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_adc_read(2u, &out));
        TEST_ASSERT_EQUAL_HEX16(0xBEEFu, out);
        TEST_ASSERT_EQUAL_UINT(0u, n_read);
    }
}

static void test_a_failed_read_is_a_timeout(void)
{
    static const esp_err_t errs[] = { ESP_ERR_INVALID_ARG, ESP_FAIL };
    unsigned i;

    for (i = 0u; i < sizeof errs / sizeof errs[0]; i++) {
        uint16_t out = 0xBEEFu;

        raw_in   = 77;
        read_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_adc_read(2u, &out));
        TEST_ASSERT_EQUAL_HEX16(0xBEEFu, out);
    }
}

static void test_the_raw_value_is_returned_whole(void)
{
    static const int raw[] = { 0, 1, 255, 256, 1023, 1024, 4095, 8191 };
    unsigned i;

    for (i = 0u; i < sizeof raw / sizeof raw[0]; i++) {
        uint16_t out = 0xBEEFu;

        raw_in = raw[i];
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_adc_read(4u, &out));
        TEST_ASSERT_EQUAL_UINT16((uint16_t)raw[i], out);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_does_nothing_and_succeeds);
    RUN_TEST(test_a_channel_past_the_count_is_refused_untouched);
    RUN_TEST(test_the_first_read_makes_unit_1_at_12_db);
    RUN_TEST(test_the_unit_is_made_once);
    RUN_TEST(test_the_reference_is_the_attenuation);
    RUN_TEST(test_a_channel_the_driver_refuses_is_out_of_range);
    RUN_TEST(test_a_failed_read_is_a_timeout);
    RUN_TEST(test_the_raw_value_is_returned_whole);
    return UNITY_END();
}
