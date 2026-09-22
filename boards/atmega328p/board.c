/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "ag/board.h"
#include "ag/config.h"
#include "ag/progmem.h"
#include "board.h"

static const ag_port_desc_t PORTS[AG_PORT_COUNT] AG_PROGMEM = {
    { AG_ADDR_PINB },   /* 0 */
    { AG_ADDR_PINC },   /* 1 */
    { AG_ADDR_PIND },   /* 2 */
};

/*
 * The pin table. Three things here are not obvious and are the reason this file
 * is unit-tested rather than trusted:
 *
 *  - Pins 5 and 6 are AG_PWM_RSVD, not AG_PWM_NONE. They have OC0B and OC0A,
 *    but Timer0 is the millisecond tick. The distinction matters to a host:
 *    "this pin has no PWM" and "this build spent its PWM on the clock" are
 *    different facts.
 *  - Pins 0 and 1 are the UART. Marked IN_USE so a host does not drive the
 *    line it is talking over.
 *  - A6 and A7 have AG_PORT_NONE. They are ADC-only on the TQFP package and
 *    absent from the DIP entirely; digital_write to either must fail rather
 *    than write into a port that is not there.
 */
static const ag_pin_desc_t PINS[AG_PIN_COUNT] AG_PROGMEM = {
/*  0  D0  PD0 RX   */ { AG_PB(2u, 0u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_IN_USE },
/*  1  D1  PD1 TX   */ { AG_PB(2u, 1u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_IN_USE },
/*  2  D2  PD2      */ { AG_PB(2u, 2u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/*  3  D3  PD3 OC2B */ { AG_PB(2u, 3u), AG_ADC_NONE, 0u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/*  4  D4  PD4      */ { AG_PB(2u, 4u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/*  5  D5  PD5 OC0B */ { AG_PB(2u, 5u), AG_ADC_NONE, AG_PWM_RSVD, AG_PIN_DIGITAL },
/*  6  D6  PD6 OC0A */ { AG_PB(2u, 6u), AG_ADC_NONE, AG_PWM_RSVD, AG_PIN_DIGITAL },
/*  7  D7  PD7      */ { AG_PB(2u, 7u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/*  8  D8  PB0      */ { AG_PB(0u, 0u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/*  9  D9  PB1 OC1A */ { AG_PB(0u, 1u), AG_ADC_NONE, 1u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/* 10  D10 PB2 OC1B */ { AG_PB(0u, 2u), AG_ADC_NONE, 2u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/* 11  D11 PB3 OC2A */ { AG_PB(0u, 3u), AG_ADC_NONE, 3u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/* 12  D12 PB4      */ { AG_PB(0u, 4u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 13  D13 PB5 LED  */ { AG_PB(0u, 5u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_LED },
/* 14  A0  PC0      */ { AG_PB(1u, 0u), 0u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 15  A1  PC1      */ { AG_PB(1u, 1u), 1u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 16  A2  PC2      */ { AG_PB(1u, 2u), 2u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 17  A3  PC3      */ { AG_PB(1u, 3u), 3u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 18  A4  PC4 SDA  */ { AG_PB(1u, 4u), 4u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 19  A5  PC5 SCL  */ { AG_PB(1u, 5u), 5u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 20  A6  ADC only */ { AG_PB(AG_PORT_NONE, 0u), 6u, AG_PWM_NONE, AG_PIN_ADC },
/* 21  A7  ADC only */ { AG_PB(AG_PORT_NONE, 0u), 7u, AG_PWM_NONE, AG_PIN_ADC },
};

uint8_t ag_board_pin_count(void) { return AG_PIN_COUNT; }

const char *ag_board_name(void) { return AG_BOARD_NAME; }

ag_status_t ag_board_pin(ag_pin_t pin, ag_pinref_t *out)
{
    uint8_t pb, port;

    if (pin >= AG_PIN_COUNT) {
        return AG_ERR_BAD_PIN;
    }
    pb   = ag_pgm_u8(&PINS[pin].port_bit);
    port = (uint8_t)(pb >> 4);
    if (port == AG_PORT_NONE) {
        return AG_ERR_UNSUPPORTED;   /* exists, but has no digital port */
    }
    out->port = ag_pgm_u16(&PORTS[port].pin_addr);
    out->bit  = (uint8_t)(pb & 0x0Fu);
    return AG_OK;
}

ag_status_t ag_board_adc_channel(ag_pin_t pin, uint8_t *out)
{
    uint8_t ch;

    if (pin >= AG_PIN_COUNT) {
        return AG_ERR_BAD_PIN;
    }
    ch = ag_pgm_u8(&PINS[pin].adc_ch);
    if (ch == AG_ADC_NONE) {
        return AG_ERR_UNSUPPORTED;
    }
    *out = ch;
    return AG_OK;
}

uint8_t ag_board_pin_flags(ag_pin_t pin)
{
    if (pin >= AG_PIN_COUNT) {
        return 0u;
    }
    return ag_pgm_u8(&PINS[pin].flags);
}
