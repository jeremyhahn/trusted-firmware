/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L15's SAADC is a later slice, so the board has no channel
 * (TFW_ADC_COUNT 0) and every read is out of range, as on mps2-tz
 * (src/hal/cortex-m/mps2/hal_adc.c). main() calls tfw_hal_adc_init on
 * every board, so it exists and succeeds; there is no reference to select.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"

tfw_status_t tfw_hal_adc_init(void)
{
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    (void)ref;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    (void)out_raw;
    if ((uint32_t)channel + 1u > TFW_ADC_COUNT) {     /* not >=: this board has none */
        return TFW_ERR_RANGE;
    }
    return TFW_ERR_UNSUPPORTED;
}
