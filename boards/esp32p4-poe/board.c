/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Two banks: GPIO 0..31 and GPIO 32..54. A pin's GPIO is base + bit. */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    {  0u },    /* 0 */
    { 32u },    /* 1 */
};

/* The reference design's free GPIOs (see board_profile.h for what is
 * reserved and why). ADC1 channel n is GPIO 16 + n on the P4. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/*  0  GPIO2   LEDC0      */ { TFW_PB(0u,  2u), TFW_ADC_NONE, 0u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  1  GPIO3   LEDC1      */ { TFW_PB(0u,  3u), TFW_ADC_NONE, 1u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  2  GPIO4   LEDC2      */ { TFW_PB(0u,  4u), TFW_ADC_NONE, 2u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  3  GPIO5   LEDC3      */ { TFW_PB(0u,  5u), TFW_ADC_NONE, 3u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  4  GPIO6              */ { TFW_PB(0u,  6u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/*  5  GPIO7   I2C SDA    */ { TFW_PB(0u,  7u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/*  6  GPIO8   I2C SCL    */ { TFW_PB(0u,  8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/*  7  GPIO20  ADC1_CH4   */ { TFW_PB(0u, 20u), 4u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/*  8  GPIO21  ADC1_CH5   */ { TFW_PB(0u, 21u), 5u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/*  9  GPIO22  ADC1_CH6   */ { TFW_PB(0u, 22u), 6u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 10  GPIO23  ADC1_CH7   */ { TFW_PB(0u, 23u), 7u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 11  GPIO32             */ { TFW_PB(1u,  0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 12  GPIO33             */ { TFW_PB(1u,  1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 13  GPIO46  SPI MOSI   */ { TFW_PB(1u, 14u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 14  GPIO47  SPI MISO   */ { TFW_PB(1u, 15u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 15  GPIO48  SPI SCK    */ { TFW_PB(1u, 16u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 16  GPIO53             */ { TFW_PB(1u, 21u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
};

/* LEDC channel, GPIO, timer, speed mode (the P4 has the low-speed group only,
 * mode 0). The tone generator takes the last timer and channel. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
    { 0u, 2u, 0u, 0u },
    { 1u, 3u, 0u, 0u },
    { 2u, 4u, 0u, 0u },
    { 3u, 5u, 0u, 0u },
};

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT,
               "PWM table does not match TFW_PWM_COUNT");
