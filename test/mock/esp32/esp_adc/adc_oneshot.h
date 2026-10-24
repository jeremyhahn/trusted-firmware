/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_adc/adc_oneshot.h and the
 * hal/adc_types.h it includes: the three calls src/hal/esp32/hal_adc.c makes
 * and the fields of the two configs it fills. test/mock/esp_pins_mock.c
 * defines the calls; the unit handle is the mock's own struct.
 */
#ifndef TFW_MOCK_ESP_ADC_ADC_ONESHOT_H
#define TFW_MOCK_ESP_ADC_ADC_ONESHOT_H

#include "esp_err.h"

typedef enum {
    ADC_UNIT_1 = 0,
    ADC_UNIT_2,
} adc_unit_t;

typedef enum {
    ADC_CHANNEL_0 = 0,
    ADC_CHANNEL_1,
    ADC_CHANNEL_2,
    ADC_CHANNEL_3,
    ADC_CHANNEL_4,
    ADC_CHANNEL_5,
    ADC_CHANNEL_6,
    ADC_CHANNEL_7,
    ADC_CHANNEL_8,
    ADC_CHANNEL_9,
} adc_channel_t;

typedef enum {
    ADC_ATTEN_DB_0   = 0,
    ADC_ATTEN_DB_2_5 = 1,
    ADC_ATTEN_DB_6   = 2,
    ADC_ATTEN_DB_12  = 3,
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_DEFAULT = 0,
} adc_bitwidth_t;

typedef struct adc_oneshot_unit_ctx_t *adc_oneshot_unit_handle_t;

typedef struct {
    adc_unit_t unit_id;
} adc_oneshot_unit_init_cfg_t;

typedef struct {
    adc_atten_t    atten;
    adc_bitwidth_t bitwidth;
} adc_oneshot_chan_cfg_t;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config,
                               adc_oneshot_unit_handle_t *ret_unit);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *config);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw);

#endif
