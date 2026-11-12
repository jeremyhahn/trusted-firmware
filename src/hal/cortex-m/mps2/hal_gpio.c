/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Digital pins on mps2: the FPGA I/O block's LED register, port index 0
 * in the board's table (boards/mps2-an505/board.c, boards/mps2-an500/board.c),
 * bit n user LED n.
 *
 * The LEDs are outputs and nothing else, so an input mode is refused with
 * TFW_ERR_UNSUPPORTED rather than accepted and ignored, and a read returns
 * the level last written, which the register reads back. A coordinate this
 * SoC has no pin at is TFW_ERR_BAD_PIN: the board table should never produce
 * one, and a wrong table must fail loudly, not light the wrong LED.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"

#define PORT_LED 0u

static uint8_t is_led(const tfw_pinref_t *pin)
{
    return (uint8_t)(pin->port == PORT_LED && pin->bit < TFW_MPS2_LED_COUNT);
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *pin, tfw_pin_mode_t mode)
{
    if (!is_led(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    return (mode == TFW_PIN_OUTPUT) ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *pin, uint8_t level)
{
    uint32_t bit, led, primask;

    if (!is_led(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    bit = 1u << pin->bit;
    /* The tone's TIMER0 handler toggles its own bit of this word: masked,
     * so an edge cannot land between the read and the store and be undone. */
    primask = tfw_cm_irq_save();
    led = TFW_MPS2_FPGAIO_LED;
    TFW_MPS2_FPGAIO_LED = level ? (led | bit) : (led & ~bit);
    tfw_cm_irq_restore(primask);
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *pin, uint8_t *level)
{
    if (!is_led(pin)) {
        return TFW_ERR_BAD_PIN;
    }
    *level = (uint8_t)((TFW_MPS2_FPGAIO_LED >> pin->bit) & 1u);
    return TFW_OK;
}
