/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 tone: a square wave on one of the FPGA I/O block's LED bits,
 * toggled from the interrupt of the CMSDK APB timer TIMER0 (soc_regs.h; its
 * base and interrupt are the board profile's). The timer reloads every half
 * period at TFW_CM_CPU_HZ, tfw_mps2_tone_reload's arithmetic.
 *
 * One tone at a time, as on the AVR and the ESP32: a second start retargets
 * the timer (and the pin, the old one left low); stop disables the timer and
 * its interrupt and leaves the pin low. A frequency the reload cannot hit is
 * refused with TFW_ERR_RANGE, as the AVR refuses one beyond its prescaler.
 *
 * The interrupt is installed as the console's is (hal_uart.c): the handler is
 * the vector table's weak tfw_cm_irq<n>, n being TFW_MPS2_TIMER0_IRQ.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"

void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_TIMER0_IRQ)(void);

static volatile uint32_t tone_mask;   /* the pin's LED bit; 0 when silent */

static void pin_low(uint32_t mask)
{
    TFW_MPS2_FPGAIO_LED = TFW_MPS2_FPGAIO_LED & ~mask;
}

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    if (hz == 0u)
        return tfw_hal_tone_stop();
    if (p->port != 0u || p->bit >= TFW_MPS2_LED_COUNT)
        return TFW_ERR_BAD_PIN;
    uint32_t reload = tfw_mps2_tone_reload((uint32_t)TFW_CM_CPU_HZ, hz);
    if (reload == 0u)
        return TFW_ERR_RANGE;

    /* Retarget: the timer stops before its reload changes, and a pending
     * interrupt from the old tone is dropped. */
    TFW_MPS2_TIMER_CTRL = 0u;
    TFW_MPS2_TIMER_INTCLEAR = 1u;
    uint32_t mask = 1uL << p->bit;
    if (tone_mask != 0u && tone_mask != mask)
        pin_low(tone_mask);
    tone_mask = mask;
    pin_low(mask);

    TFW_MPS2_TIMER_RELOAD = reload;
    TFW_MPS2_TIMER_VALUE  = reload;
    tfw_cm_nvic_enable((uint32_t)TFW_MPS2_TIMER0_IRQ);
    TFW_MPS2_TIMER_CTRL = TFW_MPS2_TIMER_CTRL_EN | TFW_MPS2_TIMER_CTRL_INTEN;
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void)
{
    TFW_MPS2_TIMER_CTRL = 0u;
    tfw_cm_nvic_disable((uint32_t)TFW_MPS2_TIMER0_IRQ);
    TFW_MPS2_TIMER_INTCLEAR = 1u;
    if (tone_mask != 0u)
        pin_low(tone_mask);
    tone_mask = 0u;
    return TFW_OK;
}

/* One edge per interrupt: clear it, then flip the pin. */
void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_TIMER0_IRQ)(void)
{
    TFW_MPS2_TIMER_INTCLEAR = 1u;
    uint32_t mask = tone_mask;
    if (mask != 0u)
        TFW_MPS2_FPGAIO_LED = TFW_MPS2_FPGAIO_LED ^ mask;
}
