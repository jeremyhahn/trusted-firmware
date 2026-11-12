/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

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
/* 1  LED1 */ { TFW_PB(1u, 10u), TFW_ADC_NONE, 0u,           TFW_PIN_DIGITAL | TFW_PIN_LED | TFW_PIN_PWM },
/* 2  LED2 */ { TFW_PB(2u,  7u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 3  LED3 */ { TFW_PB(1u, 14u), TFW_ADC_NONE, 1u,           TFW_PIN_DIGITAL | TFW_PIN_LED | TFW_PIN_PWM },
/* 4  BTN0 */ { TFW_PB(1u, 13u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 5  BTN1 */ { TFW_PB(1u,  9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 6  BTN2 */ { TFW_PB(1u,  8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 7  BTN3 */ { TFW_PB(0u,  4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
};

/* PWM20's channels 0 and 1 on LED1 (P1.10) and LED3 (P1.14), the DK's two
 * LEDs on port 1. PWM20 drives pins in its own peripheral domain, port 1:
 * Zephyr's pwm20_default puts OUT0 on P1.10
 * (boards/nordic/nrf54l15dk/nrf54l15dk_nrf54l_05_10_15-pinctrl.dtsi), and
 * nrf54l15dk_common.dtsi has led1 on P1.10 and led3 on P1.14. LED0 and LED2
 * are on port 2, which PWM20 does not reach. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
/* 0  LED1 */ { 0u, 0u, 1u, 10u },
/* 1  LED3 */ { 0u, 1u, 1u, 14u },
};

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT,
               "PWM table does not match TFW_PWM_COUNT");
