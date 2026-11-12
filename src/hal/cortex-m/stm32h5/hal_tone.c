/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Tone on the STM32H5: TIM3 channel 2 in output-compare toggle mode, on the
 * STM32H573I-DK's Arduino D3 (PB5, AF2). The timer flips the pin at each match
 * with no CPU in the loop, so a period is two matches; the prescaler and
 * reload come from tfw_h5_tone_psc_arr (h5_calc.h), host-tested alone.
 *
 * One tone at a time, as on AVR and ESP32: a second start retargets the same
 * channel, stop forces the channel's output low and halts the counter. Every
 * register and value is in soc_regs.h with its ST source; what the pinned
 * sources do not settle is on docs/boards/stm32h573i-dk.md, "What is not
 * verified". Linked only when the board sets TFW_FEATURE_TONE 1
 * (mk/firmware.mk, TONE_SRCS).
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "h5_calc.h"

static uint8_t running;

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    uint16_t psc;
    uint16_t arr;
    uint32_t bank;

    if (hz == 0u) {
        return tfw_hal_tone_stop();
    }
    /* Only the channel's own pin carries TIM3_CH2; any other would stay
     * silent while the answer said it sounded. */
    if (p->port != TFW_H5_TONE_BANK || p->bit != TFW_H5_TONE_PIN) {
        return TFW_ERR_BAD_PIN;
    }
    if (!tfw_h5_tone_psc_arr(TFW_H5_TONE_TIM_HZ, hz, &psc, &arr)) {
        return TFW_ERR_RANGE;
    }
    bank = tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, p->port);
    TFW_H5_RCC_AHB2ENR |= 1u << p->port;
    TFW_H5_TONE_RCC_ENR |= TFW_H5_TONE_RCC_EN;
    (void)TFW_H5_TONE_RCC_ENR;                  /* the enables land before the blocks are written */

    /* Halted while it is reloaded, so a retarget never runs a stale count
     * past a shorter reload. UG then loads the prescaler and zeroes CNT. */
    TFW_H5_TONE_TIM_CR1 &= ~TFW_H5_TONE_CR1_CEN;
    TFW_H5_TONE_TIM_PSC  = psc;
    TFW_H5_TONE_TIM_ARR  = arr;
    TFW_H5_TONE_TIM_CCR2 = 0u;
    TFW_H5_TONE_TIM_EGR  = TFW_H5_TONE_EGR_UG;
    TFW_H5_TONE_TIM_CCMR1 = (TFW_H5_TONE_TIM_CCMR1 & ~TFW_H5_TONE_CCMR1_CH2) | TFW_H5_TONE_OC2M_TOGGLE;
    TFW_H5_TONE_TIM_CCER  = (TFW_H5_TONE_TIM_CCER & ~TFW_H5_TONE_CCER_CH2) | TFW_H5_TONE_CCER_CC2E;

    /* The pin to the timer: its alternate function first, then the mode, so
     * it never drives as AF0 in between. */
    TFW_H5_GPIO_AFR(bank, p->bit / 8u) = tfw_h5_field4(TFW_H5_GPIO_AFR(bank, p->bit / 8u), p->bit, TFW_H5_TONE_AF);
    TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(TFW_H5_GPIO_MODER(bank), p->bit, TFW_H5_MODE_AF);

    TFW_H5_TONE_TIM_CR1 |= TFW_H5_TONE_CR1_CEN;
    running = 1u;
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void)
{
    if (running) {
        /* Forced inactive holds OC2REF low, and with CC2P clear the pin
         * follows it; then the counter stops. */
        TFW_H5_TONE_TIM_CCMR1 = (TFW_H5_TONE_TIM_CCMR1 & ~TFW_H5_TONE_CCMR1_CH2) | TFW_H5_TONE_OC2M_LOW;
        TFW_H5_TONE_TIM_CR1 &= ~TFW_H5_TONE_CR1_CEN;
        running = 0u;
    }
    return TFW_OK;
}
