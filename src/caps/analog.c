/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "analog.h"
#include "ag/board.h"
#include "ag/hal.h"

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
ag_status_t ag_analog_cmd_read(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin = ag_read_u8(r);
    uint8_t     ch;
    uint16_t    raw;
    ag_status_t st = ag_board_adc_channel(pin, &ch);

    if (st != AG_OK) {
        return st;
    }
    st = ag_hal_adc_read(ch, &raw);
    if (st != AG_OK) {
        return st;
    }
    ag_write_u16(w, raw);
    return AG_OK;
}

ag_status_t ag_analog_cmd_set_ref(ag_reader_t *r, ag_writer_t *w)
{
    uint8_t ref = ag_read_u8(r);
    (void)w;
    if (ref > (uint8_t)AG_ADC_REF_INTERNAL) {
        return AG_ERR_RANGE;
    }
    return ag_hal_adc_set_ref((ag_adc_ref_t)ref);
}
