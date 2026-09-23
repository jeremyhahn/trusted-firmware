/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "ag/board.h"
#include "ag/config.h"
#include "ag/progmem.h"
#include "board_profile.h"

/* One port. The whole table. */
const ag_port_desc_t ag_board_ports[AG_PORT_COUNT] AG_PROGMEM = {
    { AG_ADDR_PINB },   /* 0 */
};

/*
 * The pin table. Four things here are not obvious:
 *
 *  - THE ADC CHANNELS DO NOT RUN WITH THE PORT BITS. ADC0 is on PB5, ADC1 on
 *    PB2, ADC2 on PB4 and ADC3 on PB3. A table that assumed ADCn was on PBn
 *    would be right about nothing except by accident, and would read a
 *    different pin than the host asked for -- which is the worst way for a
 *    sensor reading to be wrong, because it is stable and plausible.
 *  - PINS 0 AND 1 ARE THE SOFTWARE UART. Marked IN_USE so a host does not
 *    drive the line it is talking over. On this part that is not merely
 *    inadvisable: the bit clock is Timer1 and the levels are set by an ISR, so
 *    a digital_write to pin 1 fights the transmitter directly.
 *  - EVERY COMPARE UNIT IS AG_PWM_RSVD, NOT AG_PWM_NONE. Timer0's two pins go
 *    to the millisecond tick; Timer1's go to the UART's bit clock. Reporting
 *    them absent would tell a host to move its load to another pin, when the
 *    truth is that no pin on this part will do PWM in this build and the fix is
 *    a different build. Pin 3 is included because /OC1B is Timer1's inverted
 *    output -- the same compare unit, on a second pin.
 *  - PIN 5 IS /RESET. It has a port bit and it has ADC0, and both work only
 *    under conditions a host must be told about: the ADC reads correctly while
 *    the pin stays above the reset threshold, and the pin becomes a GPIO only
 *    if RSTDISBL is programmed, which also ends ISP programming. It is marked
 *    AG_PIN_RESERVED and NOT AG_PIN_DIGITAL, so a host that respects the flags
 *    never writes it, while the descriptor still says exactly where it is.
 */
const ag_pin_desc_t ag_board_pins[AG_PIN_COUNT] AG_PROGMEM = {
/* 0  PB0  OC0A  AIN0  UART RX */ { AG_PB(0u, 0u), AG_ADC_NONE, AG_PWM_RSVD, AG_PIN_DIGITAL | AG_PIN_IN_USE },
/* 1  PB1  OC0B/OC1A   UART TX */ { AG_PB(0u, 1u), AG_ADC_NONE, AG_PWM_RSVD, AG_PIN_DIGITAL | AG_PIN_IN_USE },
/* 2  PB2  ADC1  SCK   INT0    */ { AG_PB(0u, 2u), 1u,          AG_PWM_NONE, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 3  PB3  ADC3  /OC1B         */ { AG_PB(0u, 3u), 3u,          AG_PWM_RSVD, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 4  PB4  ADC2  OC1B          */ { AG_PB(0u, 4u), 2u,          AG_PWM_RSVD, AG_PIN_DIGITAL | AG_PIN_ADC },
/* 5  PB5  ADC0  /RESET  dW    */ { AG_PB(0u, 5u), 0u,          AG_PWM_NONE, AG_PIN_ADC | AG_PIN_RESERVED },
};

/*
 * EMPTY, and deliberately so: no pin on this part can do PWM in this build.
 *
 * Timer0 is the millisecond tick and Timer1 is the software UART's bit clock,
 * which on a part with no USART is not a feature that could be traded away.
 * The pin table above carries AG_PWM_RSVD for the four pins that have a compare
 * unit, so CAPS reports "spent" rather than "absent" and a host is told the fix
 * is a different build rather than a different pin.
 *
 * A one-element array with an entry nothing indexes would be dead data that
 * looks live. AG_PWM_COUNT is 0 and so is this.
 */
const ag_pwm_desc_t ag_board_pwms[AG_PWM_COUNT] AG_PROGMEM;

/* A table one row short of its declared count zero-fills in silence, and the
 * missing pin then resolves to port 0 bit 0 -- a real pin, on a real port,
 * that the host was never told about. These turn that into a compile error. */
_Static_assert(sizeof ag_board_ports / sizeof ag_board_ports[0] == AG_PORT_COUNT,
               "port table does not match AG_PORT_COUNT");
_Static_assert(sizeof ag_board_pins / sizeof ag_board_pins[0] == AG_PIN_COUNT,
               "pin table does not match AG_PIN_COUNT");
_Static_assert(AG_PWM_COUNT == 0u,
               "PWM table does not match AG_PWM_COUNT");
