/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "analog.h"
#include "tfw/board.h"
#include "tfw/hal.h"

/*
 * Returns the RAW converter count, not a voltage and not a percentage.
 *
 * The firmware being replaced called analogRead on a moisture sensor and
 * reported the number as a percentage, which it was not, and on a flow meter
 * that outputs PULSES, where a voltage reading is meaningless. Calibration is a
 * property of the sensor and its wiring, both of which the host knows and the
 * device cannot. So the device reports what the converter measured and stops
 * there.
 */
tfw_status_t tfw_analog_cmd_read(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin = tfw_read_u8(r);
    uint8_t     ch;
    uint16_t    raw;
    tfw_status_t st = tfw_board_adc_channel(pin, &ch);

    if (st != TFW_OK) {
        return st;
    }
    st = tfw_hal_adc_read(ch, &raw);
    if (st != TFW_OK) {
        return st;
    }
    tfw_write_u16(w, raw);
    return TFW_OK;
}

tfw_status_t tfw_analog_cmd_set_ref(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t ref = tfw_read_u8(r);
    (void)w;
    if (ref > (uint8_t)TFW_ADC_REF_INTERNAL) {
        return TFW_ERR_RANGE;
    }
    return tfw_hal_adc_set_ref((tfw_adc_ref_t)ref);
}
