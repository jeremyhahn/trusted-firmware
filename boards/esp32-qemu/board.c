/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* One bank: GPIO 0..31 start at 0. */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0u },     /* 0 */
};

/* Logical pins 0..4 are GPIO 2, 4, 5, 18 and 19 -- the GPIO number is the port
 * base plus the bit, so the bit IS the GPIO number on this one-bank board. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  GPIO2   LEDC0 */ { TFW_PB(0u,  2u), TFW_ADC_NONE, 0u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 1  GPIO4   LEDC1 */ { TFW_PB(0u,  4u), TFW_ADC_NONE, 1u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 2  GPIO5   LEDC2 */ { TFW_PB(0u,  5u), TFW_ADC_NONE, 2u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 3  GPIO18  LEDC3 */ { TFW_PB(0u, 18u), TFW_ADC_NONE, 3u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 4  GPIO19        */ { TFW_PB(0u, 19u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
};

/* LEDC channel, GPIO, timer, speed mode. Mode 0 is the only mode on the C3
 * and the high-speed group on the original ESP32; both have channels 0..3 and
 * timer 0 in it. The tone generator takes the last timer and channel, which
 * no row here names. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
    { 0u,  2u, 0u, 0u },
    { 1u,  4u, 0u, 0u },
    { 2u,  5u, 0u, 0u },
    { 3u, 18u, 0u, 0u },
};

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT,
               "PWM table does not match TFW_PWM_COUNT");
