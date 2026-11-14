/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "board_profile.h"

/* Five ports, numbered as the SoC numbers its RGPIO banks from zero: port n is
 * GPIOn+1 (src/hal/cortex-m/imx95/soc_calc.c holds their addresses). */
const tfw_port_desc_t tfw_board_ports[TFW_PORT_COUNT] TFW_PROGMEM = {
    { 0u },   /* 0: GPIO1 */
    { 1u },   /* 1: GPIO2 */
    { 2u },   /* 2: GPIO3 */
    { 3u },   /* 3: GPIO4 */
    { 4u },   /* 4: GPIO5 */
};

/* The two PWM pins, GPIO2_IO02 and GPIO2_IO03, come after the pins main
 * numbered first (GPIO2_IO00, GPIO2_IO01 and, in the soft:ADC=1 row, ADC_IN0
 * and ADC_IN1), so no pin's number moves: they are pins 2 and 3, or 4 and 5
 * with ADC=1. They carry the TPM's PWM channels when PWM=1 (TFW_PWM_COUNT 2)
 * the M7's TPM is known (TFW_IMX95_TPM_BASE non-zero) and a pad can be handed
 * back to RGPIO when PWM stops (TFW_IMX95_PWM_PAD_HANDOFF, board_profile.h).
 * Until then no pin offers one: tfw_board_pwm() refuses PWM_WRITE before
 * src/caps/pwm.c makes the pin an output, and CAPS advertises none. */
#if TFW_PWM_COUNT && TFW_IMX95_PWM_LIVE
#define BOARD_PWM(n)  (n)
#define BOARD_PIN_PWM TFW_PIN_PWM
#else
#define BOARD_PWM(n)  TFW_PWM_NONE
#define BOARD_PIN_PWM 0u
#endif

/* PROVISIONAL: GPIO2_IO00 to GPIO2_IO03, digital and nothing else, until a
 * lane reads UM12527's expansion-header table and the System Manager
 * configuration that assigns pins to the M7 (TO CONFIRM). No pin is claimed
 * as an LED: the board's LEDs are not confirmed to be the M7's. */
const tfw_pin_desc_t tfw_board_pins[TFW_PIN_COUNT] TFW_PROGMEM = {
/* 0  GPIO2_IO00 */ { TFW_PB(1u, 0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 1  GPIO2_IO01 */ { TFW_PB(1u, 1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
#if TFW_CM_ADC
/* The SAR ADC's inputs, ADC only, in the soft:ADC=1 row. Which ADC inputs
 * reach a header is UM12527's, TO CONFIRM: channels 0 and 1 until then. */
/* 2  ADC_IN0    */ { TFW_PB(TFW_PORT_NONE, 0u), 0u, TFW_PWM_NONE, TFW_PIN_ADC },
/* 3  ADC_IN1    */ { TFW_PB(TFW_PORT_NONE, 0u), 1u, TFW_PWM_NONE, TFW_PIN_ADC },
#endif
/* 2|4 GPIO2_IO02 */ { TFW_PB(1u, 2u), TFW_ADC_NONE, BOARD_PWM(0u), TFW_PIN_DIGITAL | BOARD_PIN_PWM },
/* 3|5 GPIO2_IO03 */ { TFW_PB(1u, 3u), TFW_ADC_NONE, BOARD_PWM(1u), TFW_PIN_DIGITAL | BOARD_PIN_PWM },
};

#if TFW_PWM_COUNT
/* PWM=1 only (profile.mk): channels 0 and 1 of the M7's TPM
 * (src/hal/cortex-m/imx95/hal_pwm.c; the instance is TFW_IMX95_TPM_BASE, zero
 * until imx95-design.md question 17 is answered), on the two PWM pins above.
 * PROVISIONAL as the pins above: which pads carry a TPM channel to UM12527's
 * expansion header, and the System Manager's mux for them, are TO CONFIRM. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM = {
    /* unit channel port bit */
    { 0u, 0u, 1u, 2u },   /* 0: TPM CH0, GPIO2_IO02 */
    { 0u, 1u, 1u, 3u },   /* 1: TPM CH1, GPIO2_IO03 */
};
#else
/* EMPTY without PWM=1, as on mps2-an505. */
const tfw_pwm_desc_t tfw_board_pwms[TFW_PWM_COUNT] TFW_PROGMEM;
#endif

_Static_assert(sizeof tfw_board_ports / sizeof tfw_board_ports[0] == TFW_PORT_COUNT,
               "port table does not match TFW_PORT_COUNT");
_Static_assert(sizeof tfw_board_pins / sizeof tfw_board_pins[0] == TFW_PIN_COUNT,
               "pin table does not match TFW_PIN_COUNT");
_Static_assert(TFW_PWM_COUNT == 0u ||
               sizeof tfw_board_pwms / sizeof tfw_board_pwms[0] == TFW_PWM_COUNT,
               "PWM table does not match TFW_PWM_COUNT");
