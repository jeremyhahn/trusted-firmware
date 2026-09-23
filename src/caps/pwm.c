/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "pwm.h"
#include "ag/board.h"
#include "ag/hal.h"

ag_status_t ag_pwm_cmd_write(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin  = ag_read_u8(r);
    uint8_t     duty = ag_read_u8(r);
    ag_pwmref_t ref;
    ag_pinref_t pref;
    ag_status_t st;

    (void)w;
    if (ag_board_pin_flags(pin) & (AG_PIN_RESERVED | AG_PIN_IN_USE)) {
        return AG_ERR_RESERVED_PIN;
    }
    /* AG_ERR_UNSUPPORTED covers both "this pin has no compare unit" and "its
     * unit belongs to the tick timer". CAPS distinguishes them up front, so a
     * host never has to discover it here. */
    st = ag_board_pwm(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    st = ag_board_pin(pin, &pref);
    if (st != AG_OK) {
        return st;
    }
    /* The pin must be an output for the compare unit to drive it. Doing this
     * here rather than requiring the host to call pin_mode first removes a
     * step whose omission produces a PWM that silently does nothing. */
    st = ag_hal_gpio_mode(&pref, AG_PIN_OUTPUT);
    if (st != AG_OK) {
        return st;
    }
    if (duty == 0u) {
        /* Zero disconnects rather than setting a duty of zero, which would
         * hold the pin low through the timer and quietly break digital_write
         * on it afterwards. */
        return ag_hal_pwm_stop(&ref);
    }
    return ag_hal_pwm_write(&ref, duty);
}
