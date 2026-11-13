/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Four ports, each the SoC's index of a GPIO bank, 0 for GPIOA
 * (src/hal/cortex-m/stm32h5/hal_gpio.c): the two banks the LEDs are on and
 * GPIOH, the Arduino header's D5 and D6, and GPIOB, its D3. */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 5u },   /* 0  GPIOF */
    { 8u },   /* 1  GPIOI */
    { 7u },   /* 2  GPIOH */
    { 1u },   /* 3  GPIOB, the Arduino header's D3 */
};

/* The four user LEDs (ST's stm32h573i_discovery.h). Each lights at a LOW pin:
 * the HAL drives the level it is given, so 0 lights an LED and 1 puts it out. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LD1 green  PI9 */ { TFW_PB(1u, 9u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 1  LD2 orange PI8 */ { TFW_PB(1u, 8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 2  LD3 red    PF1 */ { TFW_PB(0u, 1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 3  LD4 blue   PF4 */ { TFW_PB(0u, 4u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_LED },
/* 4  ARD_D6     PH10 */ { TFW_PB(2u, 10u), TFW_ADC_NONE, 0u,          TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 5  ARD_D5     PH11 */ { TFW_PB(2u, 11u), TFW_ADC_NONE, 1u,          TFW_PIN_DIGITAL | TFW_PIN_PWM },
/* 6  ARD_D3     PB5 */  { TFW_PB(3u, 5u),  TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },   /* TIM3_CH2, the tone */
};

/* The Arduino header's D6 and D5, TIM5 channels 1 and 2 (ST's board
 * configuration for the kit, STM32_open_pin_data boards/
 * H77_Discovery_STM32H573I-DK_STM32H573IIK3Q_Board_AllConfig.ioc: PH10
 * ARD_D6 S_TIM5_CH1, PH11 ARD_D5 S_TIM5_CH2). Unit is the timer, port the
 * SoC's bank index (7, GPIOH), not a port-table row
 * (src/hal/cortex-m/stm32h5/hal_pwm.c). */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
    { 5u, 1u, 7u, 10u },   /* 0  TIM5_CH1 PH10 ARD_D6 */
    { 5u, 2u, 7u, 11u },   /* 1  TIM5_CH2 PH11 ARD_D5 */
};

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT,
               "PWM table does not match TFW_PWM_COUNT");
