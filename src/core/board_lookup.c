/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Board lookups -- the logic half of the board descriptor.
 *
 * The data half lives in boards/<board>/board.c and is nothing but three
 * tables. Everything here is identical on every classic AVR, because the
 * packing is: a port index and a bit in one byte, an ADC channel, an index into
 * the PWM table, and flags. Keeping it in one place is what makes "adding a
 * board is data" true rather than aspirational -- the previous arrangement
 * copied these ninety lines into each board, which is ninety lines that can
 * drift between two boards for no reason anyone intended.
 *
 * Still no register access: only addresses held as numbers. This file compiles
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

#include "ag/board.h"
#include "ag/config.h"
#include "ag/progmem.h"

ag_status_t ag_board_pin(ag_pin_t pin, ag_pinref_t *out)
{
    uint8_t pb, port;

    if (pin >= AG_PIN_COUNT) {
        return AG_ERR_BAD_PIN;
    }
    pb   = ag_pgm_u8(&ag_board_pins[pin].port_bit);
    port = (uint8_t)(pb >> 4);
    if (port == AG_PORT_NONE) {
        return AG_ERR_UNSUPPORTED;   /* exists, but has no digital port */
    }
    out->port = ag_pgm_u16(&ag_board_ports[port].pin_addr);
    out->bit  = (uint8_t)(pb & 0x0Fu);
    return AG_OK;
}

ag_status_t ag_board_adc_channel(ag_pin_t pin, uint8_t *out)
{
    uint8_t ch;

    if (pin >= AG_PIN_COUNT) {
        return AG_ERR_BAD_PIN;
    }
    ch = ag_pgm_u8(&ag_board_pins[pin].adc_ch);
    if (ch == AG_ADC_NONE) {
        return AG_ERR_UNSUPPORTED;
    }
    *out = ch;
    return AG_OK;
}

ag_status_t ag_board_pwm(ag_pin_t pin, ag_pwmref_t *out)
{
    uint8_t idx;

    if (pin >= AG_PIN_COUNT) {
        return AG_ERR_BAD_PIN;
    }
    idx = ag_pgm_u8(&ag_board_pins[pin].pwm_idx);
    /* AG_PWM_NONE and AG_PWM_RSVD are both refusals here, and the board table
     * keeps them distinct only so CAPS can tell a host WHICH refusal it is. */
    if (idx == AG_PWM_NONE || idx == AG_PWM_RSVD) {
        return AG_ERR_UNSUPPORTED;
    }
    out->tccra   = ag_pgm_u16(&ag_board_pwms[idx].tccra);
    out->ocr     = ag_pgm_u16(&ag_board_pwms[idx].ocr);
    out->com_bit = ag_pgm_u8(&ag_board_pwms[idx].com_bit);
    out->is16    = ag_pgm_u8(&ag_board_pwms[idx].is16);
    return AG_OK;
}

uint8_t ag_board_pin_port_bit(ag_pin_t pin)
{
    if (pin >= AG_PIN_COUNT) {
        return 0xFFu;
    }
    return ag_pgm_u8(&ag_board_pins[pin].port_bit);
}

uint8_t ag_board_pin_pwm_state(ag_pin_t pin)
{
    uint8_t idx;

    if (pin >= AG_PIN_COUNT) {
        return 0u;
    }
    idx = ag_pgm_u8(&ag_board_pins[pin].pwm_idx);
    if (idx == AG_PWM_NONE) { return 0u; }
    if (idx == AG_PWM_RSVD) { return 2u; }
    return 1u;
}

uint8_t ag_board_pin_flags(ag_pin_t pin)
{
    if (pin >= AG_PIN_COUNT) {
        return 0u;
    }
    return ag_pgm_u8(&ag_board_pins[pin].flags);
}

uint8_t ag_board_pin_count(void) { return AG_PIN_COUNT; }

const char *ag_board_name(void) { return AG_BOARD_NAME; }
