/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* One port, numbered as the SoC numbers its GPIO: port 0 is SIO's bank
 * (src/hal/cortex-m/rp2350/hal_gpio.c). Bit n is GPIOn. */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0u },   /* 0 */
};

/* The header GPIOs, in GPIO order. GPIO0-1 (UART0) and GPIO23-25, 29 (the
 * CYW43439) are not pins. adc_ch is the converter's input: GPIO26-28 are
 * ADC0-2 (src/hal/cortex-m/rp2350/hal_adc.c); ADC3, GPIO29, is the radio's
 * SPI clock and the VSYS divider, and the temperature sensor is not a pin. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  GP2  */ { TFW_PB(0u,  2u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 1  GP3  */ { TFW_PB(0u,  3u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 2  GP4  */ { TFW_PB(0u,  4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 3  GP5  */ { TFW_PB(0u,  5u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 4  GP6  */ { TFW_PB(0u,  6u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 5  GP7  */ { TFW_PB(0u,  7u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 6  GP8  */ { TFW_PB(0u,  8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 7  GP9  */ { TFW_PB(0u,  9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 8  GP10 */ { TFW_PB(0u, 10u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 9  GP11 */ { TFW_PB(0u, 11u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 10 GP12 */ { TFW_PB(0u, 12u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 11 GP13 */ { TFW_PB(0u, 13u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 12 GP14 */ { TFW_PB(0u, 14u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 13 GP15 */ { TFW_PB(0u, 15u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 14 GP16 */ { TFW_PB(0u, 16u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 15 GP17 */ { TFW_PB(0u, 17u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 16 GP18 */ { TFW_PB(0u, 18u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 17 GP19 */ { TFW_PB(0u, 19u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 18 GP20 */ { TFW_PB(0u, 20u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 19 GP21 */ { TFW_PB(0u, 21u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 20 GP22 */ { TFW_PB(0u, 22u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 21 GP26 */ { TFW_PB(0u, 26u), 0u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 22 GP27 */ { TFW_PB(0u, 27u), 1u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 23 GP28 */ { TFW_PB(0u, 28u), 2u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
};

/* EMPTY, as on mps2-an505: this slice drives no PWM slice, and a row nothing
 * indexes would be dead data that looks live. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(TFW_PWM_COUNT == 0u,
               "PWM table does not match TFW_PWM_COUNT");
