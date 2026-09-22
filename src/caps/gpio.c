/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "gpio.h"
#include "ag/board.h"
#include "ag/hal.h"

/*
 * Resolves a logical pin and refuses the ones this build must not touch.
 *
 * A pin the firmware is using for something else is not merely inadvisable to
 * drive -- driving pin 1 cuts the serial line the command arrived on, and the
 * host never learns why the device stopped answering. Refusing it is the only
 * behaviour that leaves a diagnosable system.
 */
static ag_status_t resolve_writable(ag_pin_t pin, ag_pinref_t *ref)
{
    uint8_t     flags = ag_board_pin_flags(pin);
    ag_status_t st    = ag_board_pin(pin, ref);

    if (st != AG_OK) {
        return st;
    }
    if (flags & (AG_PIN_RESERVED | AG_PIN_IN_USE)) {
        return AG_ERR_RESERVED_PIN;
    }
    return AG_OK;
}

ag_status_t ag_gpio_cmd_mode(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t      pin  = ag_read_u8(r);
    uint8_t       mode = ag_read_u8(r);
    ag_pinref_t   ref;
    ag_status_t   st;

    (void)w;
    st = resolve_writable(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    if (mode > (uint8_t)AG_PIN_INPUT_PULLUP) {
        return AG_ERR_RANGE;
    }
    return ag_hal_gpio_mode(&ref, (ag_pin_mode_t)mode);
}

/* Reading is permitted on a reserved pin: observing the UART line is harmless,
 * and a blanket refusal would make diagnosing a wiring problem harder than it
 * needs to be. Only driving is refused. */
ag_status_t ag_gpio_cmd_read(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin = ag_read_u8(r);
    ag_pinref_t ref;
    ag_status_t st;
    uint8_t     level;

    st = ag_board_pin(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    st = ag_hal_gpio_read(&ref, &level);
    if (st != AG_OK) {
        return st;
    }
    ag_write_u8(w, level);
    return AG_OK;
}

ag_status_t ag_gpio_cmd_write(ag_reader_t *r, ag_writer_t *w)
{
    ag_pin_t    pin   = ag_read_u8(r);
    uint8_t     level = ag_read_u8(r);
    ag_pinref_t ref;
    ag_status_t st;

    (void)w;
    st = resolve_writable(pin, &ref);
    if (st != AG_OK) {
        return st;
    }
    if (level > 1u) {
        return AG_ERR_RANGE;
    }
    return ag_hal_gpio_write(&ref, level);
}
