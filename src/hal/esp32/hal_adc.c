/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Analog read through ESP-IDF's oneshot ADC driver, on ADC unit 1. The board
 * table's adc_ch is the ADC1 channel number.
 *
 * The unit is created on the first read, not in tfw_hal_adc_init: main()
 * calls init on every board, and an emulator that models no ADC (Espressif's
 * QEMU models none) must still boot. A part whose ADC cannot be brought up
 * answers the read with TFW_ERR_UNSUPPORTED instead.
 *
 * There is no reference to select on this part, only an input attenuation:
 * VCC maps to the widest range (12 dB, about 3.1 V full scale) and INTERNAL
 * to none (about 0.95 V). An external reference does not exist and is
 * refused. The raw value is the converter's 12 bits, not the AVR's 10; the
 * host reads the width from the board, as it reads everything else.
 */

#include "esp_adc/adc_oneshot.h"

#include "tfw/hal.h"
#include "tfw/config.h"

static adc_oneshot_unit_handle_t unit;
static adc_atten_t               atten = ADC_ATTEN_DB_12;
static uint8_t                   failed;

tfw_status_t tfw_hal_adc_init(void)
{
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    switch (ref) {
    case TFW_ADC_REF_VCC:      atten = ADC_ATTEN_DB_12; return TFW_OK;
    case TFW_ADC_REF_INTERNAL: atten = ADC_ATTEN_DB_0;  return TFW_OK;
    default:                   return TFW_ERR_UNSUPPORTED;
    }
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    adc_oneshot_chan_cfg_t cfg = { .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT };
    int raw = 0;

    if ((uint32_t)channel + 1u > TFW_ADC_COUNT) {     /* not >=: a board may have none */
        return TFW_ERR_RANGE;
    }
    if (unit == NULL && !failed) {
        adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
        if (adc_oneshot_new_unit(&ucfg, &unit) != ESP_OK) {
            failed = 1u;
        }
    }
    if (unit == NULL) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (adc_oneshot_config_channel(unit, (adc_channel_t)channel, &cfg) != ESP_OK) {
        return TFW_ERR_RANGE;
    }
    if (adc_oneshot_read(unit, (adc_channel_t)channel, &raw) != ESP_OK) {
        return TFW_ERR_TIMEOUT;
    }
    *out_raw = (uint16_t)raw;
    return TFW_OK;
}
