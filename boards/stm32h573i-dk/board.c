/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Two ports, each the SoC's index of a GPIO bank, 0 for GPIOA
 * (src/hal/cortex-m/stm32h5/hal_gpio.c): the two banks the LEDs are on. */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 5u },   /* 0  GPIOF */
    { 8u },   /* 1  GPIOI */
};

/* The four user LEDs (ST's stm32h573i_discovery.h). Each lights at a LOW pin:
 * the HAL drives the level it is given, so 0 lights an LED and 1 puts it out. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LD1 green  PI9 */ { TFW_PB(1u, 9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 1  LD2 orange PI8 */ { TFW_PB(1u, 8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 2  LD3 red    PF1 */ { TFW_PB(0u, 1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 3  LD4 blue   PF4 */ { TFW_PB(0u, 4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
};

/* EMPTY, as on mps2-an505: no pin here offers PWM yet, and a row nothing
 * indexes would be dead data that looks live. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(TFW_PWM_COUNT == 0u,
               "PWM table does not match TFW_PWM_COUNT");
