/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Five ports, numbered as the SoC numbers its RGPIO banks from zero: port n is
 * GPIOn+1 (src/hal/cortex-m/imx95/soc_calc.c holds their addresses). */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0u },   /* 0: GPIO1 */
    { 1u },   /* 1: GPIO2 */
    { 2u },   /* 2: GPIO3 */
    { 3u },   /* 3: GPIO4 */
    { 4u },   /* 4: GPIO5 */
};

/* PROVISIONAL: GPIO2_IO00 and GPIO2_IO01, digital and nothing else, until a
 * lane reads UM12527's expansion-header table and the System Manager
 * configuration that assigns pins to the M7 (TO CONFIRM). No pin is claimed
 * as an LED: the board's LEDs are not confirmed to be the M7's. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  GPIO2_IO00 */ { TFW_PB(1u, 0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 1  GPIO2_IO01 */ { TFW_PB(1u, 1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
};

/* EMPTY, as on mps2-an505: no PWM this port drives yet. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(TFW_PWM_COUNT == 0u,
               "PWM table does not match TFW_PWM_COUNT");
