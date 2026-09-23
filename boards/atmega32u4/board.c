/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "ag/board.h"
#include "ag/config.h"
#include "ag/progmem.h"
#include "board_profile.h"

const ag_port_desc_t ag_board_ports[AG_PORT_COUNT] AG_PROGMEM = {
    { AG_ADDR_PINB },   /* 0 */
    { AG_ADDR_PINC },   /* 1 */
    { AG_ADDR_PIND },   /* 2 */
    { AG_ADDR_PINE },   /* 3 */
    { AG_ADDR_PINF },   /* 4 */
};

/*
 * The pin table. The 32U4 differs from the 328P in five ways that are each a
 * reason this file is unit-tested rather than trusted:
 *
 *  - The logical numbering is not the port order. D0..D3 are PD2, PD3, PD1,
 *    PD0 -- the low nibble of PORTD, reversed in pairs, because the Leonardo
 *    routes RX/TX to the USART1 pins and SDA/SCL to the TWI pins and numbers
 *    them in the order they appear on the header. There is no arithmetic
 *    relation between pin number and port bit anywhere on this part, so every
 *    row is a fact from the datasheet rather than a pattern.
 *
 *  - Pins 3 and 11 are AG_PWM_RSVD, not AG_PWM_NONE. PD0 has OC0B and PB7 has
 *    OC0A, but Timer0 is the millisecond tick. The distinction matters to a
 *    host: "this pin has no PWM" and "this build spent its PWM on the clock"
 *    are different facts. PB7 also carries Timer1's third compare unit, OC1C,
 *    which this build does not expose -- claiming it would make pin 11 the one
 *    pin whose PWM survives the tick on this board and not on any other, and a
 *    capability that exists on one board's pin 11 and not another's is worse
 *    than one that exists on neither.
 *
 *  - Pins 0 and 1 are the UART. Marked IN_USE so a host does not drive the
 *    line it is talking over. On this part that is USART1 on PD2/PD3 -- the
 *    TTL pins on the header, NOT the USB connector. The 32U4's primary serial
 *    port on a stock Leonardo is USB CDC, and this firmware does not implement
 *    USB CDC: it speaks the wire protocol over USART1 at AG_UART_BAUD and a
 *    host reaches it through a USB-to-serial adapter on pins 0 and 1. Plugging
 *    the board's own USB port into a host will enumerate nothing.
 *
 *  - Six pins are both digital and analog: 4, 6, 8, 9, 10 and 12 carry ADC8,
 *    ADC10, ADC11, ADC12, ADC13 and ADC9. The Arduino core calls those A6..A11
 *    and gives them second numbers; this descriptor does not, because two rows
 *    on one port bit is the collision the tests exist to catch. The channel
 *    lives on the digital number instead.
 *
 *  - Unlike the 328P's A6 and A7, NO pin on this part is ADC-only. Every ADC
 *    input is a real port pin, so AG_PORT_NONE appears nowhere below and every
 *    pin can be driven as well as read.
 */
const ag_pin_desc_t ag_board_pins[AG_PIN_COUNT] AG_PROGMEM = {
/*  0  D0  PD2 RX1    */ { AG_PB(2u, 2u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_IN_USE },
/*  1  D1  PD3 TX1    */ { AG_PB(2u, 3u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_IN_USE },
/*  2  D2  PD1 SDA    */ { AG_PB(2u, 1u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/*  3  D3  PD0 SCL    */ { AG_PB(2u, 0u), AG_ADC_NONE, AG_PWM_RSVD, AG_PIN_DIGITAL },
/*  4  D4  PD4 ADC8   */ { AG_PB(2u, 4u), 8u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/*  5  D5  PC6 OC3A   */ { AG_PB(1u, 6u), AG_ADC_NONE, 2u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/*  6  D6  PD7 ADC10  */ { AG_PB(2u, 7u), 10u,         AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/*  7  D7  PE6        */ { AG_PB(3u, 6u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/*  8  D8  PB4 ADC11  */ { AG_PB(0u, 4u), 11u,         AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/*  9  D9  PB5 OC1A   */ { AG_PB(0u, 5u), 12u,         0u,          AG_PIN_DIGITAL | AG_PIN_PWM | AG_PIN_ADC },
/* 10  D10 PB6 OC1B   */ { AG_PB(0u, 6u), 13u,         1u,          AG_PIN_DIGITAL | AG_PIN_PWM | AG_PIN_ADC },
/* 11  D11 PB7 OC0A   */ { AG_PB(0u, 7u), AG_ADC_NONE, AG_PWM_RSVD, AG_PIN_DIGITAL },
/* 12  D12 PD6 ADC9   */ { AG_PB(2u, 6u), 9u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 13  D13 PC7 LED    */ { AG_PB(1u, 7u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_LED },
/* 14  D14 PB3 MISO   */ { AG_PB(0u, 3u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 15  D15 PB1 SCK    */ { AG_PB(0u, 1u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 16  D16 PB2 MOSI   */ { AG_PB(0u, 2u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 17  D17 PB0 SS     */ { AG_PB(0u, 0u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 18  A0  PF7 ADC7   */ { AG_PB(4u, 7u), 7u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 19  A1  PF6 ADC6   */ { AG_PB(4u, 6u), 6u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 20  A2  PF5 ADC5   */ { AG_PB(4u, 5u), 5u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 21  A3  PF4 ADC4   */ { AG_PB(4u, 4u), 4u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 22  A4  PF1 ADC1   */ { AG_PB(4u, 1u), 1u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 23  A5  PF0 ADC0   */ { AG_PB(4u, 0u), 0u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
};

/* The three usable output-compare units, all 16-bit.
 *
 * Timer0's two are absent by design: it drives the millisecond tick, and the
 * pin table marks pins 3 and 11 AG_PWM_RSVD so CAPS can tell a host the
 * difference between "no PWM here" and "this build spent it on the clock".
 *
 * Timer4's are absent for a different reason, and the difference matters: OC4A
 * (pin 13) and OC4D (pin 6) are real and unclaimed, but the high-speed timer
 * needs PWM4x enables and a TC4H high-byte write before a duty cycle means
 * anything, and ag_hal_pwm_write does neither. Those pins report no PWM rather
 * than a PWM that would not work. */
const ag_pwm_desc_t ag_board_pwms[AG_PWM_COUNT] AG_PROGMEM = {
/* 0  OC1A, pin 9  */ { AG_ADDR_TCCR1A, AG_ADDR_OCR1AL, COM1A1_BIT, 1u },
/* 1  OC1B, pin 10 */ { AG_ADDR_TCCR1A, AG_ADDR_OCR1BL, COM1B1_BIT, 1u },
/* 2  OC3A, pin 5  */ { AG_ADDR_TCCR3A, AG_ADDR_OCR3AL, COM3A1_BIT, 1u },
};

/* A table one row short of its declared count zero-fills in silence, and the
 * missing pin then resolves to port 0 bit 0 -- a real pin, on a real port,
 * that the host was never told about. These turn that into a compile error. */
_Static_assert(sizeof ag_board_ports / sizeof ag_board_ports[0] == AG_PORT_COUNT,
               "port table does not match AG_PORT_COUNT");
_Static_assert(sizeof ag_board_pins / sizeof ag_board_pins[0] == AG_PIN_COUNT,
               "pin table does not match AG_PIN_COUNT");
_Static_assert(sizeof ag_board_pwms / sizeof ag_board_pwms[0] == AG_PWM_COUNT,
               "PWM table does not match AG_PWM_COUNT");
