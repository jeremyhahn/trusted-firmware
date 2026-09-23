/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "ag/board.h"
#include "ag/config.h"
#include "ag/progmem.h"
#include "board_profile.h"

/* Eleven ports, in the order the profile indexes them. There is no port I --
 * the letter is skipped across the AVR family so it cannot be read as a digit
 * -- so index 8 is J, and an off-by-one from here down would map every high
 * pin to the port next door. */
const ag_port_desc_t ag_board_ports[AG_PORT_COUNT] AG_PROGMEM = {
    { AG_ADDR_PINA },   /*  0 */
    { AG_ADDR_PINB },   /*  1 */
    { AG_ADDR_PINC },   /*  2 */
    { AG_ADDR_PIND },   /*  3 */
    { AG_ADDR_PINE },   /*  4 */
    { AG_ADDR_PINF },   /*  5 */
    { AG_ADDR_PING },   /*  6 */
    { AG_ADDR_PINH },   /*  7 */
    { AG_ADDR_PINJ },   /*  8 */
    { AG_ADDR_PINK },   /*  9 */
    { AG_ADDR_PINL },   /* 10 */
};

/*
 * The pin table.
 *
 * The Mega's header numbering bears almost no relation to its port layout, and
 * that is the whole reason this table is written out rather than computed. Five
 * things here are worth knowing before reading it:
 *
 *  - Pins 4 and 13 are AG_PWM_RSVD, not AG_PWM_NONE. PG5 carries OC0B and PB7
 *    carries OC0A, but Timer0 is the millisecond tick. The distinction matters
 *    to a host: "this pin has no PWM" and "this build spent its PWM on the
 *    clock" are different facts, and only one of them might change.
 *  - Pin 13 is also OC1C, so the pin is not physically out of PWM -- Timer1
 *    could drive it. It stays reserved anyway: it is the pin the on-board LED
 *    hangs off, and handing a host a duty cycle on the one pin every bring-up
 *    blinks trades a confusing failure for a marginal capability.
 *  - Pins 0 and 1 are USART0, the line this firmware is talking over. Marked
 *    IN_USE so a host does not drive the branch it is sitting on. The other
 *    three USARTs are left as plain GPIO, because this build does not use them.
 *  - Pins 30..37 count DOWN the port: pin 30 is PC7 and pin 37 is PC0. Pins
 *    42..49 do the same on port L. Both are printed that way on the header, and
 *    both are the kind of reversal a reader assumes is a typo.
 *  - Every pin has a digital port. The 328P's ADC-only A6 and A7 have no
 *    counterpart here, so nothing in this table carries AG_PORT_NONE.
 */
const ag_pin_desc_t ag_board_pins[AG_PIN_COUNT] AG_PROGMEM = {
/*  0  D0  PE0 RX0  */ { AG_PB(4u, 0u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_IN_USE },
/*  1  D1  PE1 TX0  */ { AG_PB(4u, 1u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_IN_USE },
/*  2  D2  PE4 OC3B */ { AG_PB(4u, 4u),  AG_ADC_NONE, 0u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/*  3  D3  PE5 OC3C */ { AG_PB(4u, 5u),  AG_ADC_NONE, 1u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/*  4  D4  PG5 OC0B */ { AG_PB(6u, 5u),  AG_ADC_NONE, AG_PWM_RSVD, AG_PIN_DIGITAL },
/*  5  D5  PE3 OC3A */ { AG_PB(4u, 3u),  AG_ADC_NONE, 2u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/*  6  D6  PH3 OC4A */ { AG_PB(7u, 3u),  AG_ADC_NONE, 3u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/*  7  D7  PH4 OC4B */ { AG_PB(7u, 4u),  AG_ADC_NONE, 4u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/*  8  D8  PH5 OC4C */ { AG_PB(7u, 5u),  AG_ADC_NONE, 5u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/*  9  D9  PH6 OC2B */ { AG_PB(7u, 6u),  AG_ADC_NONE, 6u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/* 10  D10 PB4 OC2A */ { AG_PB(1u, 4u),  AG_ADC_NONE, 7u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/* 11  D11 PB5 OC1A */ { AG_PB(1u, 5u),  AG_ADC_NONE, 8u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/* 12  D12 PB6 OC1B */ { AG_PB(1u, 6u),  AG_ADC_NONE, 9u,          AG_PIN_DIGITAL | AG_PIN_PWM },
/* 13  D13 PB7 LED  */ { AG_PB(1u, 7u),  AG_ADC_NONE, AG_PWM_RSVD, AG_PIN_DIGITAL | AG_PIN_LED },
/* 14  D14 PJ1 TX3  */ { AG_PB(8u, 1u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 15  D15 PJ0 RX3  */ { AG_PB(8u, 0u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 16  D16 PH1 TX2  */ { AG_PB(7u, 1u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 17  D17 PH0 RX2  */ { AG_PB(7u, 0u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 18  D18 PD3 TX1  */ { AG_PB(3u, 3u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 19  D19 PD2 RX1  */ { AG_PB(3u, 2u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 20  D20 PD1 SDA  */ { AG_PB(3u, 1u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 21  D21 PD0 SCL  */ { AG_PB(3u, 0u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 22  D22 PA0      */ { AG_PB(0u, 0u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 23  D23 PA1      */ { AG_PB(0u, 1u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 24  D24 PA2      */ { AG_PB(0u, 2u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 25  D25 PA3      */ { AG_PB(0u, 3u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 26  D26 PA4      */ { AG_PB(0u, 4u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 27  D27 PA5      */ { AG_PB(0u, 5u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 28  D28 PA6      */ { AG_PB(0u, 6u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 29  D29 PA7      */ { AG_PB(0u, 7u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 30  D30 PC7      */ { AG_PB(2u, 7u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 31  D31 PC6      */ { AG_PB(2u, 6u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 32  D32 PC5      */ { AG_PB(2u, 5u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 33  D33 PC4      */ { AG_PB(2u, 4u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 34  D34 PC3      */ { AG_PB(2u, 3u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 35  D35 PC2      */ { AG_PB(2u, 2u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 36  D36 PC1      */ { AG_PB(2u, 1u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 37  D37 PC0      */ { AG_PB(2u, 0u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 38  D38 PD7      */ { AG_PB(3u, 7u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 39  D39 PG2      */ { AG_PB(6u, 2u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 40  D40 PG1      */ { AG_PB(6u, 1u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 41  D41 PG0      */ { AG_PB(6u, 0u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 42  D42 PL7      */ { AG_PB(10u, 7u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 43  D43 PL6      */ { AG_PB(10u, 6u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 44  D44 PL5 OC5C */ { AG_PB(10u, 5u), AG_ADC_NONE, 10u,         AG_PIN_DIGITAL | AG_PIN_PWM },
/* 45  D45 PL4 OC5B */ { AG_PB(10u, 4u), AG_ADC_NONE, 11u,         AG_PIN_DIGITAL | AG_PIN_PWM },
/* 46  D46 PL3 OC5A */ { AG_PB(10u, 3u), AG_ADC_NONE, 12u,         AG_PIN_DIGITAL | AG_PIN_PWM },
/* 47  D47 PL2      */ { AG_PB(10u, 2u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 48  D48 PL1      */ { AG_PB(10u, 1u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 49  D49 PL0      */ { AG_PB(10u, 0u), AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 50  D50 PB3 MISO */ { AG_PB(1u, 3u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 51  D51 PB2 MOSI */ { AG_PB(1u, 2u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 52  D52 PB1 SCK  */ { AG_PB(1u, 1u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 53  D53 PB0 SS   */ { AG_PB(1u, 0u),  AG_ADC_NONE, AG_PWM_NONE, AG_PIN_DIGITAL },
/* 54  A0  PF0      */ { AG_PB(5u, 0u),  0u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 55  A1  PF1      */ { AG_PB(5u, 1u),  1u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 56  A2  PF2      */ { AG_PB(5u, 2u),  2u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 57  A3  PF3      */ { AG_PB(5u, 3u),  3u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 58  A4  PF4      */ { AG_PB(5u, 4u),  4u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 59  A5  PF5      */ { AG_PB(5u, 5u),  5u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 60  A6  PF6      */ { AG_PB(5u, 6u),  6u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 61  A7  PF7      */ { AG_PB(5u, 7u),  7u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 62  A8  PK0      */ { AG_PB(9u, 0u),  8u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 63  A9  PK1      */ { AG_PB(9u, 1u),  9u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 64  A10 PK2      */ { AG_PB(9u, 2u),  10u,         AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 65  A11 PK3      */ { AG_PB(9u, 3u),  11u,         AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 66  A12 PK4      */ { AG_PB(9u, 4u),  12u,         AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 67  A13 PK5      */ { AG_PB(9u, 5u),  13u,         AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 68  A14 PK6      */ { AG_PB(9u, 6u),  14u,         AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 69  A15 PK7      */ { AG_PB(9u, 7u),  15u,         AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
};

/* The thirteen usable output-compare units, ordered by the header pin they
 * drive so this table can be read against the one above.
 *
 * Timer0's two are absent by design: it drives the millisecond tick, and the
 * pin table marks pins 4 and 13 AG_PWM_RSVD so CAPS can tell a host the
 * difference between "no PWM here" and "this build spent it on the clock".
 *
 * is16 is what tells ag_hal_pwm_write to store the compare value as a word.
 * Timers 1, 3, 4 and 5 are 16-bit and the table names the LOW byte of each
 * OCR; Timer2 is 8-bit, and marking it 16 would write a zero into whatever
 * register follows it. */
const ag_pwm_desc_t ag_board_pwms[AG_PWM_COUNT] AG_PROGMEM = {
/*  0  OC3B, pin 2  */ { AG_ADDR_TCCR3A, AG_ADDR_OCR3BL, COM3B1_BIT, 1u },
/*  1  OC3C, pin 3  */ { AG_ADDR_TCCR3A, AG_ADDR_OCR3CL, COM3C1_BIT, 1u },
/*  2  OC3A, pin 5  */ { AG_ADDR_TCCR3A, AG_ADDR_OCR3AL, COM3A1_BIT, 1u },
/*  3  OC4A, pin 6  */ { AG_ADDR_TCCR4A, AG_ADDR_OCR4AL, COM4A1_BIT, 1u },
/*  4  OC4B, pin 7  */ { AG_ADDR_TCCR4A, AG_ADDR_OCR4BL, COM4B1_BIT, 1u },
/*  5  OC4C, pin 8  */ { AG_ADDR_TCCR4A, AG_ADDR_OCR4CL, COM4C1_BIT, 1u },
/*  6  OC2B, pin 9  */ { AG_ADDR_TCCR2A, AG_ADDR_OCR2B,  COM2B1_BIT, 0u },
/*  7  OC2A, pin 10 */ { AG_ADDR_TCCR2A, AG_ADDR_OCR2A,  COM2A1_BIT, 0u },
/*  8  OC1A, pin 11 */ { AG_ADDR_TCCR1A, AG_ADDR_OCR1AL, COM1A1_BIT, 1u },
/*  9  OC1B, pin 12 */ { AG_ADDR_TCCR1A, AG_ADDR_OCR1BL, COM1B1_BIT, 1u },
/* 10  OC5C, pin 44 */ { AG_ADDR_TCCR5A, AG_ADDR_OCR5CL, COM5C1_BIT, 1u },
/* 11  OC5B, pin 45 */ { AG_ADDR_TCCR5A, AG_ADDR_OCR5BL, COM5B1_BIT, 1u },
/* 12  OC5A, pin 46 */ { AG_ADDR_TCCR5A, AG_ADDR_OCR5AL, COM5A1_BIT, 1u },
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
