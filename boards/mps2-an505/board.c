/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Two ports, numbered as the SoC's GPIO numbers its banks: port 0 is the FPGA
 * I/O block's LED register (src/hal/cortex-m/mps2/hal_gpio.c), bit n user LED
 * n; port 1 is its MISC register, whose bit 8 is the shield 0 SPI select
 * (src/hal/cortex-m/mps2/pl022.h). */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0u },   /* 0 */
    { 1u },   /* 1 */
};

/* The two user LEDs: outputs only. A read returns the level last written.
 * Then the shield 0 SPI select, MISC bit 8, SHIELD0_SPI_nCS (AN505, Arm DAI
 * 0505B, 4.6 and 4.14 Table 4-5): SPI_XFER's chip select in the SPI=1 row.
 * hal_bus.c drives it around each transfer and hal_gpio.c refuses it, so it
 * is not a digital pin; nothing claims it, so SPI_XFER accepts it. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  LED0 */ { TFW_PB(0u, 0u), TFW_ADC_NONE, TFW_MPS2_LED0_PWM, TFW_PIN_DIGITAL | TFW_PIN_LED | (TFW_PIN_PWM & TFW_MPS2_PWM_FLAG) },
/* 1  LED1 */ { TFW_PB(0u, 1u), TFW_ADC_NONE, TFW_MPS2_LED1_PWM, TFW_PIN_DIGITAL | TFW_PIN_LED | (TFW_PIN_PWM & TFW_MPS2_PWM_FLAG) },
/* 2  nCS  */ { TFW_PB(1u, 8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
};

#if TFW_FEATURE_PWM
/* The PWM=1 row (TFW_CM_PWM): software PWM on the CMSDK dual timer
 * (src/hal/cortex-m/mps2/hal_pwm.c), which drives each LED from the timer's
 * interrupt through hal_gpio.c. A row is { unit, channel, port, bit }: unit 0
 * is the dual timer, the channel indexes hal_pwm.c's state, and port and bit
 * are the LED's, as the pin table has them. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
    { 0u, 0u, 0u, 0u },   /* 0  LED0 */
    { 0u, 1u, 0u, 1u },   /* 1  LED1 */
};
#else
/* EMPTY outside the PWM=1 row, as on the ATtiny85: no pin here can do PWM, and
 * a row nothing indexes would be dead data that looks live. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;
#endif

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT
               || (TFW_PWM_COUNT == 0u && !TFW_FEATURE_PWM),
               "PWM table does not match TFW_PWM_COUNT");
