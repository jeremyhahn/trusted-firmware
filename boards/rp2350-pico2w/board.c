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
 * SPI clock and the VSYS divider, and the temperature sensor is not a pin.
 * In the I2C=1 row GP4 and GP5 are I2C0's SDA and SCL
 * (src/hal/cortex-m/rp2350/hal_bus.c) and are taken (PICO2W_PIN_I2C); in the
 * default row they are plain GPIOs. Taken, GPIO_MODE or a scheduled toggle
 * cannot hand them back to SIO and cut the bus. */
#define PICO2W_PIN_I2C ((TFW_FEATURE_I2C) ? TFW_PIN_IN_USE : 0u)

const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  GP2  */ { TFW_PB(0u,  2u), TFW_ADC_NONE, 0u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 1  GP3  */ { TFW_PB(0u,  3u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 2  GP4  */ { TFW_PB(0u,  4u), TFW_ADC_NONE, 1u,           TFW_PIN_DIGITAL | TFW_PIN_PWM | PICO2W_PIN_I2C },  /* SDA */
/* 3  GP5  */ { TFW_PB(0u,  5u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | PICO2W_PIN_I2C },  /* SCL */
/* 4  GP6  */ { TFW_PB(0u,  6u), TFW_ADC_NONE, 2u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 5  GP7  */ { TFW_PB(0u,  7u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 6  GP8  */ { TFW_PB(0u,  8u), TFW_ADC_NONE, 3u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 7  GP9  */ { TFW_PB(0u,  9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 8  GP10 */ { TFW_PB(0u, 10u), TFW_ADC_NONE, 4u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 9  GP11 */ { TFW_PB(0u, 11u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 10 GP12 */ { TFW_PB(0u, 12u), TFW_ADC_NONE, 5u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 11 GP13 */ { TFW_PB(0u, 13u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 12 GP14 */ { TFW_PB(0u, 14u), TFW_ADC_NONE, 6u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 13 GP15 */ { TFW_PB(0u, 15u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 14 GP16 */ { TFW_PB(0u, 16u), TFW_ADC_NONE, 7u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
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

/* One header GPIO per slice, each channel A: GPIO n is slice (n >> 1) & 7,
 * channel n & 1 (src/hal/cortex-m/rp2350/hal_pwm.c). Stopping a pin disables
 * its whole slice, so no two rows share one. { slice, channel, port, GPIO }. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
    { 1u, 0u, 0u,  2u },   /* 0 GP2  */
    { 2u, 0u, 0u,  4u },   /* 1 GP4  */
    { 3u, 0u, 0u,  6u },   /* 2 GP6  */
    { 4u, 0u, 0u,  8u },   /* 3 GP8  */
    { 5u, 0u, 0u, 10u },   /* 4 GP10 */
    { 6u, 0u, 0u, 12u },   /* 5 GP12 */
    { 7u, 0u, 0u, 14u },   /* 6 GP14 */
    { 0u, 0u, 0u, 16u },   /* 7 GP16 */
};

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT,
               "PWM table does not match TFW_PWM_COUNT");
