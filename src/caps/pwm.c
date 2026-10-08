/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "pwm.h"
#include "tfw/board.h"
#include "tfw/hal.h"

tfw_status_t tfw_pwm_cmd_write(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin  = tfw_read_u8(r);
    uint8_t     duty = tfw_read_u8(r);
    tfw_pwmref_t ref;
    tfw_pinref_t pref;
    tfw_status_t st;

    (void)w;
    if (tfw_board_pin_flags(pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
        return TFW_ERR_RESERVED_PIN;
    }
    /* TFW_ERR_UNSUPPORTED covers both "this pin has no compare unit" and "its
     * unit belongs to the tick timer". CAPS distinguishes them up front, so a
     * host never has to discover it here. */
    st = tfw_board_pwm(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_board_pin(pin, &pref);
    if (st != TFW_OK) {
        return st;
    }
    /* The pin must be an output for the compare unit to drive it. Doing this
     * here rather than requiring the host to call pin_mode first removes a
     * step whose omission produces a PWM that silently does nothing. */
    st = tfw_hal_gpio_mode(&pref, TFW_PIN_OUTPUT);
    if (st != TFW_OK) {
        return st;
    }
    if (duty == 0u) {
        /* Zero disconnects rather than setting a duty of zero, which would
         * hold the pin low through the timer and quietly break digital_write
         * on it afterwards. */
        return tfw_hal_pwm_stop(&ref);
    }
    return tfw_hal_pwm_write(&ref, duty);
}
