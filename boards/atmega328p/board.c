/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { TFW_ADDR_PINB },   /* 0 */
    { TFW_ADDR_PINC },   /* 1 */
    { TFW_ADDR_PIND },   /* 2 */
};

/*
 * The pin table. Three things here are not obvious and are the reason this file
 * is unit-tested rather than trusted:
 *
 *  - Pins 5 and 6 are TFW_PWM_RSVD, not TFW_PWM_NONE. They have OC0B and OC0A,
 *    but Timer0 is the millisecond tick. The distinction matters to a host:
 *    "this pin has no PWM" and "this build spent its PWM on the clock" are
 *    different facts.
 *  - Pins 0 and 1 are the UART. Marked IN_USE so a host does not drive the
 *    line it is talking over.
 *  - A6 and A7 have TFW_PORT_NONE. They are ADC-only on the TQFP package and
 *    absent from the DIP entirely; digital_write to either must fail rather
 *    than write into a port that is not there.
 */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/*  0  D0  PD0 RX   */ { TFW_PB(2u, 0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_IN_USE },
/*  1  D1  PD1 TX   */ { TFW_PB(2u, 1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_IN_USE },
/*  2  D2  PD2      */ { TFW_PB(2u, 2u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/*  3  D3  PD3 OC2B */ { TFW_PB(2u, 3u), TFW_ADC_NONE, 0u,          TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  4  D4  PD4      */ { TFW_PB(2u, 4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ETH },
/*  5  D5  PD5 OC0B */ { TFW_PB(2u, 5u), TFW_ADC_NONE, TFW_PWM_RSVD, TFW_PIN_DIGITAL },
/*  6  D6  PD6 OC0A */ { TFW_PB(2u, 6u), TFW_ADC_NONE, TFW_PWM_RSVD, TFW_PIN_DIGITAL },
/*  7  D7  PD7      */ { TFW_PB(2u, 7u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/*  8  D8  PB0      */ { TFW_PB(0u, 0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_OTA_STORE },
/*  9  D9  PB1 OC1A */ { TFW_PB(0u, 1u), TFW_ADC_NONE, 1u,          TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 10  D10 PB2 OC1B */ { TFW_PB(0u, 2u), TFW_ADC_NONE, 2u,          TFW_PIN_DIGITAL | TFW_PIN_PWM | TFW_PIN_ETH },
/* 11  D11 PB3 OC2A */ { TFW_PB(0u, 3u), TFW_ADC_NONE, 3u,          TFW_PIN_DIGITAL | TFW_PIN_PWM | TFW_PIN_ETH },
/* 12  D12 PB4      */ { TFW_PB(0u, 4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ETH },
/* 13  D13 PB5 LED  */ { TFW_PB(0u, 5u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED | TFW_PIN_ETH },
/* 14  A0  PC0      */ { TFW_PB(1u, 0u), 0u,          TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 15  A1  PC1      */ { TFW_PB(1u, 1u), 1u,          TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 16  A2  PC2      */ { TFW_PB(1u, 2u), 2u,          TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 17  A3  PC3      */ { TFW_PB(1u, 3u), 3u,          TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 18  A4  PC4 SDA  */ { TFW_PB(1u, 4u), 4u,          TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 19  A5  PC5 SCL  */ { TFW_PB(1u, 5u), 5u,          TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 20  A6  ADC only */ { TFW_PB(TFW_PORT_NONE, 0u), 6u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 21  A7  ADC only */ { TFW_PB(TFW_PORT_NONE, 0u), 7u, TFW_PWM_NONE, TFW_PIN_ADC },
};

/* The four usable output-compare units. Timer0's two are absent by design: it
 * drives the millisecond tick, and the pin table marks pins 5 and 6 TFW_PWM_RSVD
 * so CAPS can tell a host the difference between "no PWM here" and "this build
 * spent it on the clock". */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
/* 0  OC2B, pin 3  */ { TFW_ADDR_TCCR2A, TFW_ADDR_OCR2B,  COM2B1_BIT, 0u },
/* 1  OC1A, pin 9  */ { TFW_ADDR_TCCR1A, TFW_ADDR_OCR1AL, COM1A1_BIT, 1u },
/* 2  OC1B, pin 10 */ { TFW_ADDR_TCCR1A, TFW_ADDR_OCR1BL, COM1B1_BIT, 1u },
/* 3  OC2A, pin 11 */ { TFW_ADDR_TCCR2A, TFW_ADDR_OCR2A,  COM2A1_BIT, 0u },
};

/* A table one row short of its declared count zero-fills in silence, and the
 * missing pin then resolves to port 0 bit 0 -- a real pin, on a real port,
 * that the host was never told about. These turn that into a compile error. */
_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT,
               "PWM table does not match TFW_PWM_COUNT");
