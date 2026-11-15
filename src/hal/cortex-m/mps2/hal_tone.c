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

/* A read-modify-write of the LED word: the caller holds PRIMASK, so neither
 * TIMER0's nor the PWM row's dual-timer interrupt (hal_pwm.c, which drives
 * the same word through hal_gpio.c) can land between the read and the
 * store and have its edge undone. */
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
     * interrupt from the old tone is dropped, in the NVIC as well as the
     * timer: the interrupt stays masked until the pins and tone_mask have
     * moved, so an edge cannot raise the old pin after it was lowered. The
     * LED writes themselves run with PRIMASK set (pin_low), so the PWM
     * row's dual-timer interrupt cannot land inside one either. */
    TFW_MPS2_TIMER_CTRL = 0u;
    tfw_cm_nvic_disable((uint32_t)TFW_MPS2_TIMER0_IRQ);
    TFW_MPS2_TIMER_INTCLEAR = 1u;
    uint32_t mask = 1uL << p->bit;
    uint32_t key = tfw_cm_irq_save();
    if (tone_mask != 0u && tone_mask != mask)
        pin_low(tone_mask);
    tone_mask = mask;
    pin_low(mask);
    tfw_cm_irq_restore(key);

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
    uint32_t key = tfw_cm_irq_save();
    if (tone_mask != 0u)
        pin_low(tone_mask);
    tone_mask = 0u;
    tfw_cm_irq_restore(key);
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
