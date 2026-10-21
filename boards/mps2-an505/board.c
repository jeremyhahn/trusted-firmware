/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* One port, numbered as the SoC's GPIO numbers its banks: port 0 is the FPGA
 * I/O block's LED register (src/hal/cortex-m/mps2-tz/hal_gpio.c). Bit n is
 * user LED n. */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0u },   /* 0 */
};

/* The two user LEDs: outputs only. A read returns the level last written. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LED0 */ { TFW_PB(0u, 0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 1  LED1 */ { TFW_PB(0u, 1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
};

/* EMPTY, as on the ATtiny85: no pin here can do PWM, and a row nothing
 * indexes would be dead data that looks live. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(TFW_PWM_COUNT == 0u,
               "PWM table does not match TFW_PWM_COUNT");
