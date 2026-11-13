/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* The TWIM's SCL and SDA, taken as TFW_PIN_ETH takes the Ethernet pins when
 * the image links the bus: a GPIO or scheduler write to either would fight
 * the TWI. Private to this board. */
#define TFW_PIN_I2C ((TFW_FEATURE_I2C) ? TFW_PIN_IN_USE : 0u)

/* Three ports, numbered as the SoC numbers its GPIO banks: P0, P1 and P2
 * (src/hal/cortex-m/nrf54l/hal_gpio.c maps the number to the bank). */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0u },   /* 0  P0 */
    { 1u },   /* 1  P1 */
    { 2u },   /* 2  P2 */
};

/* The DK's four LEDs, lit by a high level, then its four buttons, which pull
 * the pin low when pressed and want TFW_PIN_INPUT_PULLUP (Zephyr's
 * boards/nordic/nrf54l15dk/nrf54l15dk_common.dtsi). */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LED0 */ { TFW_PB(2u,  9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 1  LED1 */ { TFW_PB(1u, 10u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED | TFW_PIN_I2C },
/* 2  LED2 */ { TFW_PB(2u,  7u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 3  LED3 */ { TFW_PB(1u, 14u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 4  BTN0 */ { TFW_PB(1u, 13u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 5  BTN1 */ { TFW_PB(1u,  9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 6  BTN2 */ { TFW_PB(1u,  8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_I2C },
/* 7  BTN3 */ { TFW_PB(0u,  4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
};

/* EMPTY, as on mps2-an505: the PWM peripherals are a later slice, and a row
 * nothing indexes would be dead data that looks live. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(TFW_PWM_COUNT == 0u,
               "PWM table does not match TFW_PWM_COUNT");
