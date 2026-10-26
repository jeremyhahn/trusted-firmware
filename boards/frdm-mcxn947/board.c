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
 * which read 0 while pressed (UM12018; the SDK's board.h). */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LED_RED   P0_10 */ { TFW_PB(0u, 10u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 1  LED_GREEN P0_27 */ { TFW_PB(0u, 27u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 2  LED_BLUE  P1_2  */ { TFW_PB(1u,  2u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 3  SW2       P0_23 */ { TFW_PB(0u, 23u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 4  SW3       P0_6  */ { TFW_PB(0u,  6u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
};

/* EMPTY, as on mps2-an505: this port drives no timer, and a row nothing
 * indexes would be dead data that looks live. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(TFW_PWM_COUNT == 0u,
               "PWM table does not match TFW_PWM_COUNT");
