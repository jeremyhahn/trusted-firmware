/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Board lookups -- the logic half of the board descriptor.
 *
 * The data half lives in boards/<board>/board.c and is nothing but three
 * tables. Everything here is identical on every architecture, because a pin
 * row is: a coordinate (port index and bit), an ADC channel, an index into the
 * PWM table, and flags. How the coordinate packs is the architecture's
 * (hal_types.h), and this file reads it only through tfw_pin_coord_load /
 * _port / _bit -- never a shift or a mask, so it assumes no width. Keeping it in one place is what makes "adding a
 * board is data" true rather than aspirational -- the previous arrangement
 * copied these ninety lines into each board, which is ninety lines that can
 * drift between two boards for no reason anyone intended.
 *
 * Still no register access: the descriptor rows are the architecture's
 * (hal_types.h) and this file turns them into references only through
 * tfw_pinref_resolve / tfw_pwmref_resolve, never by naming a field. It compiles
 * and runs on the host, so the real pin table of a real part is unit-tested on
 * x86, and a wrong port bit for pin 13 is a failing test rather than a wrong
 * LED on a bench.
 *
 * NAMED board_lookup.c AND NOT board.c. Every board directory already has a
 * board.c, and gcov names its data file after the source's BASENAME -- so two
 * board.c files in one link produced one .gcda for both and `make coverage`
 * failed with an error about a working directory it could not infer, which
 * names neither file and points at neither cause.
 */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"

tfw_status_t tfw_board_pin(tfw_pin_t pin, tfw_pinref_t *out)
{
    tfw_pin_coord_t pc;
    uint8_t         port;

    if (pin >= TFW_PIN_COUNT) {
        return TFW_ERR_BAD_PIN;
    }
    pc   = tfw_pin_coord_load(&tfw_board_pins[pin].port_bit);
    port = tfw_pin_coord_port(pc);
    if (port == TFW_PORT_NONE) {
        return TFW_ERR_UNSUPPORTED;   /* exists, but has no digital port */
    }
    tfw_pinref_resolve(out, &tfw_board_ports[port], tfw_pin_coord_bit(pc));
    return TFW_OK;
}

tfw_status_t tfw_board_adc_channel(tfw_pin_t pin, uint8_t *out)
{
    uint8_t ch;

    if (pin >= TFW_PIN_COUNT) {
        return TFW_ERR_BAD_PIN;
    }
    ch = tfw_pgm_u8(&tfw_board_pins[pin].adc_ch);
    if (ch == TFW_ADC_NONE) {
        return TFW_ERR_UNSUPPORTED;
    }
    *out = ch;
    return TFW_OK;
}

tfw_status_t tfw_board_pwm(tfw_pin_t pin, tfw_pwmref_t *out)
{
    uint8_t idx;

    if (pin >= TFW_PIN_COUNT) {
        return TFW_ERR_BAD_PIN;
    }
    idx = tfw_pgm_u8(&tfw_board_pins[pin].pwm_idx);
    /* TFW_PWM_NONE and TFW_PWM_RSVD are both refusals here, and the board table
     * keeps them distinct only so CAPS can tell a host WHICH refusal it is. */
    if (idx == TFW_PWM_NONE || idx == TFW_PWM_RSVD) {
        return TFW_ERR_UNSUPPORTED;
    }
    tfw_pwmref_resolve(out, &tfw_board_pwms[idx]);
    return TFW_OK;
}

static uint8_t pwm_state_of(uint8_t idx)
{
    if (idx == TFW_PWM_NONE) { return 0u; }
    if (idx == TFW_PWM_RSVD) { return 2u; }
    return 1u;
}

uint8_t tfw_board_pin_pwm_state(tfw_pin_t pin)
{
    if (pin >= TFW_PIN_COUNT) {
        return 0u;
    }
    return pwm_state_of(tfw_pgm_u8(&tfw_board_pins[pin].pwm_idx));
}

tfw_status_t tfw_board_pin_page(tfw_pin_t pin, uint8_t *out)
{
    tfw_pin_coord_t pc;
    uint8_t         port, bit;

    if (pin >= TFW_PIN_COUNT) {
        return TFW_ERR_BAD_PIN;
    }
    pc   = tfw_pin_coord_load(&tfw_board_pins[pin].port_bit);
    port = tfw_pin_coord_port(pc);
    bit  = tfw_pin_coord_bit(pc);
    if (port == TFW_PORT_NONE) {
        port = TFW_WIRE_PORT_NONE;
        bit  = TFW_WIRE_PORT_NONE;
    }
    out[0] = pin;
    out[1] = tfw_pgm_u8(&tfw_board_pins[pin].flags);
    out[2] = port;
    out[3] = bit;
    /* TFW_ADC_NONE is already the wire's "none", so the byte goes out as stored. */
    out[4] = tfw_pgm_u8(&tfw_board_pins[pin].adc_ch);
    out[5] = pwm_state_of(tfw_pgm_u8(&tfw_board_pins[pin].pwm_idx));
    return TFW_OK;
}

uint8_t tfw_board_pin_flags(tfw_pin_t pin)
{
    if (pin >= TFW_PIN_COUNT) {
        return 0u;
    }
    return tfw_pgm_u8(&tfw_board_pins[pin].flags);
}

uint8_t tfw_board_pin_count(void) { return TFW_PIN_COUNT; }

const char *tfw_board_name(void) { return TFW_BOARD_NAME; }
