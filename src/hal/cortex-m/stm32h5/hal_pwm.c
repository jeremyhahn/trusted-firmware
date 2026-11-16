/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * PWM on TIM5, a general-purpose timer nothing else in this port drives. A
 * tfw_pwmref_t names the timer (unit, TFW_H5_PWM_UNIT), its channel (1 to 4)
 * and the GPIO, port the SoC's bank index (0 for GPIOA) and bit the pin.
 *
 * Eight-bit duty at about 1 kHz, as the AVR's analogWrite and the ESP32's
 * LEDC give: ARR 254, PSC from TFW_CM_CPU_HZ (h5_calc.h), PWM mode 1 with
 * preload, so duty 0 is never high and duty 255 always is. The timer is
 * configured on the first write, not at boot, and the pin is handed to the
 * timer (alternate function 2) on every write. Stop disables
 * the channel and returns the pin to an output driven low.
 *
 * Registers and bits: soc_regs.h and h5_calc.h, each cited there. Built and
 * host-tested, unchanged, against modeled registers (test/unit/test_stm32h5_pwm.c); not
 * yet run on the board.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "h5_calc.h"

/* Channels whose pin the timer holds, one bit each from channel 1. */
static uint8_t pwm_live;

static int valid(const tfw_pwmref_t *p)
{
    return p->unit == TFW_H5_PWM_UNIT && p->channel >= 1u && p->channel <= 4u &&
           p->port < TFW_H5_GPIO_BANKS && p->bit < TFW_H5_GPIO_PINS;
}

static uint32_t bank_of(const tfw_pwmref_t *p)
{
    return tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, p->port);
}

/* The timer's base, once: clock on, prescaler, period, preload, an update
 * event so PSC and ARR are the counter's before it starts. */
static void timer_start(void)
{
    if ((TFW_H5_TIM_CR1 & TFW_H5_TIM_CR1_CEN) != 0u) {
        return;
    }
    TFW_H5_RCC_APB1LENR |= TFW_H5_RCC_APB1LENR_TIM5EN;
    (void)TFW_H5_RCC_APB1LENR;                  /* the enable lands before the timer is written */
    TFW_H5_TIM_PSC = tfw_h5_pwm_psc(TFW_CM_CPU_HZ);
    TFW_H5_TIM_ARR = TFW_H5_PWM_ARR;
    TFW_H5_TIM_CR1 |= TFW_H5_TIM_CR1_ARPE;
    TFW_H5_TIM_EGR = TFW_H5_TIM_EGR_UG;
    TFW_H5_TIM_CR1 |= TFW_H5_TIM_CR1_CEN;
}

static void pin_to_timer(const tfw_pwmref_t *p)
{
    uint32_t bank = bank_of(p);

    TFW_H5_RCC_AHB2ENR |= 1u << p->port;
    (void)TFW_H5_RCC_AHB2ENR;                   /* the enable lands before the bank is written */
    if (p->bit < 8u) {
        TFW_H5_GPIO_AFRL(bank) = tfw_h5_field4(TFW_H5_GPIO_AFRL(bank), p->bit, TFW_H5_PWM_AF);
    } else {
        TFW_H5_GPIO_AFRH(bank) = tfw_h5_field4(TFW_H5_GPIO_AFRH(bank), p->bit, TFW_H5_PWM_AF);
    }
    TFW_H5_GPIO_PUPDR(bank) = tfw_h5_field2(TFW_H5_GPIO_PUPDR(bank), p->bit, TFW_H5_PULL_NONE);
    TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(TFW_H5_GPIO_MODER(bank), p->bit, TFW_H5_MODE_AF);
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    uint8_t live;

    if (!valid(p)) {
        return TFW_ERR_BAD_PIN;
    }
    live = (uint8_t)(1u << (p->channel - 1u));
    timer_start();
    TFW_H5_TIM_CCR(p->channel) = duty;
    if ((pwm_live & live) == 0u) {
        if (p->channel <= 2u) {
            TFW_H5_TIM_CCMR1 = tfw_h5_pwm_ccmr(TFW_H5_TIM_CCMR1, p->channel);
        } else {
            TFW_H5_TIM_CCMR2 = tfw_h5_pwm_ccmr(TFW_H5_TIM_CCMR2, p->channel);
        }
        TFW_H5_TIM_CCER = tfw_h5_pwm_ccer(TFW_H5_TIM_CCER, p->channel, 1u);
        pwm_live |= live;
    }
    /* Every write: the PWM command makes the pin a GPIO output before each
     * one (src/caps/pwm.c), so the timer gets it back each time. */
    pin_to_timer(p);
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    uint32_t bank;

    if (!valid(p)) {
        return TFW_ERR_BAD_PIN;
    }
    bank = bank_of(p);
    if ((pwm_live & (1u << (p->channel - 1u))) != 0u) {
        TFW_H5_TIM_CCER = tfw_h5_pwm_ccer(TFW_H5_TIM_CCER, p->channel, 0u);
        pwm_live &= (uint8_t)~(1u << (p->channel - 1u));
    }
    TFW_H5_RCC_AHB2ENR |= 1u << p->port;
    (void)TFW_H5_RCC_AHB2ENR;
    TFW_H5_GPIO_BSRR(bank) = 1u << (p->bit + 16u);   /* low before it drives */
    TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(TFW_H5_GPIO_MODER(bank), p->bit, TFW_H5_MODE_OUTPUT);
    return TFW_OK;
}
