/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Two ports, numbered as the SoC numbers its GPIO banks: port n is GPIOn with
 * its PORTn pin controller (src/hal/cortex-m/mcxn94x/hal_gpio.c). */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0u },   /* 0  GPIO0 */
    { 1u },   /* 1  GPIO1 */
};

/* The RGB LED, active low (a write of 0 lights it), and the two user switches,
 * which read 0 while pressed (UM12018; the SDK's board.h). Then the Arduino
 * header's analog pins A2..A4; A5 is SW2's pin. adc_ch is the LPADC0 input,
 * the channel number plus 32 for side B (src/hal/cortex-m/mcxn94x/hal_adc.c),
 * pinned in docs/reference/README.md. A0 and A1 have no cited source. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LED_RED   P0_10 */ { TFW_PB(0u, 10u), TFW_ADC_NONE, 0u,           TFW_PIN_DIGITAL | TFW_PIN_LED | TFW_PIN_PWM },
/* 1  LED_GREEN P0_27 */ { TFW_PB(0u, 27u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 2  LED_BLUE  P1_2  */ { TFW_PB(1u,  2u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 3  SW2, A5   P0_23 */ { TFW_PB(0u, 23u), 15u,          TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 4  SW3       P0_6  */ { TFW_PB(0u,  6u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 5  A2        P0_14 */ { TFW_PB(0u, 14u), 32u + 14u,    TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 6  A3        P0_22 */ { TFW_PB(0u, 22u), 14u,          TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 7  A4        P0_15 */ { TFW_PB(0u, 15u), 32u + 15u,    TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
};

/* CTIMER0 in PWM mode (src/hal/cortex-m/mcxn94x/hal_pwm.c): { unit = CTIMER,
 * channel = match 0-2, port, bit }. P0_10 is CT0_MAT0 at pin function 4
 * (TFW_MCXN_PWM_PIN_FN; the SDK's ctimer simple_pwm example pin_mux.c). */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
/* 0  CT0_MAT0, pin 0 P0_10 */ { 0u, 0u, 0u, 10u },
};

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT,
               "PWM table does not match TFW_PWM_COUNT");
