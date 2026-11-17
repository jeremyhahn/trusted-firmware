/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Three ports, each the SoC's index of a GPIO bank, 0 for GPIOA
 * (src/hal/cortex-m/stm32h5/hal_gpio.c): the three banks the LEDs are on. */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 1u },   /* 0  GPIOB */
    { 5u },   /* 1  GPIOF */
    { 6u },   /* 2  GPIOG */
};

/* The three user LEDs (UM3115, TO CONFIRM). Each lights at a HIGH pin on a
 * Nucleo-144: the HAL drives the level it is given, so 1 lights an LED. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LD1 green  PB0 */ { TFW_PB(0u, 0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 1  LD2 yellow PF4 */ { TFW_PB(1u, 4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 2  LD3 red    PG4 */ { TFW_PB(2u, 4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
};

/* EMPTY, as on the STM32H573I-DK: no pin here offers PWM yet. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(TFW_PWM_COUNT == 0u,
               "PWM table does not match TFW_PWM_COUNT");
