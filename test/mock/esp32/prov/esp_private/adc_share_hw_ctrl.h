/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_private/adc_share_hw_ctrl.h, for
 * test/unit/test_esp_prov_uds_mutate.c alone: the ADC unit lock the entropy
 * source shares, with ESP-IDF's unit values (hal/adc_types.h). The test
 * implements the calls.
 */
#ifndef TFW_MOCK_ESP32_PROV_ADC_SHARE_HW_CTRL_H
#define TFW_MOCK_ESP32_PROV_ADC_SHARE_HW_CTRL_H

#include "esp_err.h"

typedef enum {
    ADC_UNIT_1 = 0,
    ADC_UNIT_2 = 1,
} adc_unit_t;

esp_err_t adc_lock_try_acquire(adc_unit_t adc_unit);
esp_err_t adc_lock_release(adc_unit_t adc_unit);

#endif
