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
/* 1  LED1 */ { TFW_PB(1u, 10u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 2  LED2 */ { TFW_PB(2u,  7u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 3  LED3 */ { TFW_PB(1u, 14u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 4  BTN0 */ { TFW_PB(1u, 13u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 5  BTN1 */ { TFW_PB(1u,  9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 6  BTN2 */ { TFW_PB(1u,  8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 7  BTN3 */ { TFW_PB(0u,  4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* The SAADC's AIN2-AIN5, the board's ADC channels 0-3
 * (src/hal/cortex-m/nrf54l/soc_tables.c; nrfx soc/nrfx_soc_defines.h,
 * SAADC_EXTERNAL_AIN_PSELS). AIN0 and AIN1 are the console UARTE's pins and
 * AIN6 and AIN7 BTN0 and LED3 (Zephyr's nrf54l15dk_common.dtsi). */
/* 8  AIN2 */ { TFW_PB(1u,  6u), 0u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 9  AIN3 */ { TFW_PB(1u,  7u), 1u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 10 AIN4 */ { TFW_PB(1u, 11u), 2u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 11 AIN5 */ { TFW_PB(1u, 12u), 3u, TFW_PWM_NONE, TFW_PIN_ADC },
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
