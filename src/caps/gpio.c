/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "gpio.h"
#include "tfw/board.h"
#include "tfw/hal.h"

/*
 * Resolves a logical pin and refuses the ones this build must not touch.
 *
 * A pin the firmware is using for something else is not merely inadvisable to
 * drive -- driving pin 1 cuts the serial line the command arrived on, and the
 * host never learns why the device stopped answering. Refusing it is the only
 * behaviour that leaves a diagnosable system.
 */
static tfw_status_t resolve_writable(tfw_pin_t pin, tfw_pinref_t *ref)
{
    uint8_t     flags = tfw_board_pin_flags(pin);
    tfw_status_t st    = tfw_board_pin(pin, ref);

    if (st != TFW_OK) {
        return st;
    }
    if (flags & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
        return TFW_ERR_RESERVED_PIN;
    }
    return TFW_OK;
}

tfw_status_t tfw_gpio_cmd_mode(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t      pin  = tfw_read_u8(r);
    uint8_t       mode = tfw_read_u8(r);
    tfw_pinref_t   ref;
    tfw_status_t   st;

    (void)w;
    st = resolve_writable(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    if (mode > (uint8_t)TFW_PIN_INPUT_PULLUP) {
        return TFW_ERR_RANGE;
    }
    return tfw_hal_gpio_mode(&ref, (tfw_pin_mode_t)mode);
}

/* Reading is permitted on a reserved pin: observing the UART line is harmless,
 * and a blanket refusal would make diagnosing a wiring problem harder than it
 * needs to be. Only driving is refused. */
tfw_status_t tfw_gpio_cmd_read(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin = tfw_read_u8(r);
    tfw_pinref_t ref;
    tfw_status_t st;
    uint8_t     level;

    st = tfw_board_pin(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_hal_gpio_read(&ref, &level);
    if (st != TFW_OK) {
        return st;
    }
    tfw_write_u8(w, level);
    return TFW_OK;
}

tfw_status_t tfw_gpio_cmd_write(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_pin_t    pin   = tfw_read_u8(r);
    uint8_t     level = tfw_read_u8(r);
    tfw_pinref_t ref;
    tfw_status_t st;

    (void)w;
    st = resolve_writable(pin, &ref);
    if (st != TFW_OK) {
        return st;
    }
    if (level > 1u) {
        return TFW_ERR_RANGE;
    }
    return tfw_hal_gpio_write(&ref, level);
}
