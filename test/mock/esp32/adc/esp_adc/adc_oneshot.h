/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_adc/adc_oneshot.h, for
 * test/unit/test_esp_adc_mutate.c and test_esp_adc_nounit_mutate.c alone:
 * the types, values and calls src/hal/esp32/hal_adc.c uses, with ESP-IDF's
 * values. The tests implement the calls.
 */
#ifndef TFW_MOCK_ESP32_ADC_ONESHOT_H
#define TFW_MOCK_ESP32_ADC_ONESHOT_H

#include <stddef.h>
#include "esp_err.h"

typedef enum {
    ADC_UNIT_1 = 0,
    ADC_UNIT_2 = 1,
} adc_unit_t;

typedef enum {
    ADC_ATTEN_DB_0   = 0,
    ADC_ATTEN_DB_2_5 = 1,
    ADC_ATTEN_DB_6   = 2,
    ADC_ATTEN_DB_12  = 3,
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_DEFAULT = 0,
    ADC_BITWIDTH_9       = 9,
    ADC_BITWIDTH_10      = 10,
    ADC_BITWIDTH_11      = 11,
    ADC_BITWIDTH_12      = 12,
    ADC_BITWIDTH_13      = 13,
} adc_bitwidth_t;

typedef int adc_channel_t;
typedef int adc_oneshot_clk_src_t;
typedef enum { ADC_ULP_MODE_DISABLE = 0 } adc_ulp_mode_t;

typedef struct adc_oneshot_unit_ctx_t *adc_oneshot_unit_handle_t;

typedef struct {
    adc_unit_t            unit_id;
    adc_oneshot_clk_src_t clk_src;
    adc_ulp_mode_t        ulp_mode;
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
