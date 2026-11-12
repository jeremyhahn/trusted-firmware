/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Four ports, each the SoC's index of a GPIO bank, 0 for GPIOA
 * (src/hal/cortex-m/stm32h5/hal_gpio.c): the LEDs' two banks, then the
 * Arduino analog pins' other two. */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 5u },   /* 0  GPIOF */
    { 8u },   /* 1  GPIOI */
    { 0u },   /* 2  GPIOA */
    { 1u },   /* 3  GPIOB */
};

/* The four user LEDs (ST's stm32h573i_discovery.h). Each lights at a LOW pin:
 * the HAL drives the level it is given, so 0 lights an LED and 1 puts it out.
 *
 * Then the Arduino header's A0 to A5, at the pins Zephyr's board names
 * (boards/st/stm32h573i_dk/arduino_r3_connector.dtsi), each at its ADC1
 * input as hal_stm32's pin table for the part gives it
 * (dts/st/h5/stm32h573iikxq-pinctrl.dtsi, adc1_inpN_pXX); both pinned, at a
 * commit and SHA-256, in docs/reference/README.md. adc_ch is the INP number.
 * The pins are analog only; a GPIO command can still reach them, so the ADC
 * HAL puts a pin back to analog, no pull, before each read of it. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LD1 green  PI9 */ { TFW_PB(1u, 9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 1  LD2 orange PI8 */ { TFW_PB(1u, 8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 2  LD3 red    PF1 */ { TFW_PB(0u, 1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 3  LD4 blue   PF4 */ { TFW_PB(0u, 4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 4  A0  PB0  INP9  */ { TFW_PB(3u, 0u),  9u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 5  A1  PA4  INP18 */ { TFW_PB(2u, 4u), 18u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 6  A2  PA0  INP0  */ { TFW_PB(2u, 0u),  0u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 7  A3  PA5  INP19 */ { TFW_PB(2u, 5u), 19u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 8  A4  PA6  INP3  */ { TFW_PB(2u, 6u),  3u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 9  A5  PF12 INP6  */ { TFW_PB(0u, 12u), 6u, TFW_PWM_NONE, TFW_PIN_ADC },
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
