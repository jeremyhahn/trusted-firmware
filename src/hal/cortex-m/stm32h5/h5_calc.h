/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The arithmetic the STM32H5 files do before they touch a register: the
 * USART's divisor, the flash wait states a clock needs, the watchdog's
 * prescaler and reload, and the two- and four-bit fields of a GPIO bank.
 *
 * Pure functions of their arguments, with no register and no global, so the
 * host build tests them (test/unit/test_stm32h5_calc.c) and both images
 * include them: the application's SoC files and tfw-boot's UART. A register
 * write is the caller's; nothing here can be wrong in a way only a board
 * would show.
 */
#ifndef TFW_CM_STM32H5_CALC_H
#define TFW_CM_STM32H5_CALC_H

#include <stdint.h>

/* The USART's BRR for `baud` from its kernel clock, oversampling by 16 (OVER8
 * 0): the clock over the rate, rounded to the nearest. The USART takes 16 to
 * 0xFFFF; 0 is returned for a rate outside that, and for a zero rate, which
 * no BRR is. */
#define TFW_H5_USART_BRR_MIN 16u
#define TFW_H5_USART_BRR_MAX 0xFFFFu

static inline uint32_t tfw_h5_usart_brr(uint32_t kernel_hz, uint32_t baud)
{
    uint32_t div;

    if (baud == 0u) {
        return 0u;
    }
    div = (uint32_t)(((uint64_t)kernel_hz + baud / 2u) / baud);
    if (div < TFW_H5_USART_BRR_MIN || div > TFW_H5_USART_BRR_MAX) {
        return 0u;
    }
    return div;
}

/* The flash wait states the core clock needs in voltage scaling range 3, the
 * range the part leaves reset in: 20 MHz a state, to 100 MHz, VOS3's highest
 * (ST's stm32h5xx_ll_utils.c, its VOS3 latency table). Above that, 0xFF:
 * there is no setting, and the caller refuses the clock. */
#define TFW_H5_FLASH_WS_NONE 0xFFu

static inline uint8_t tfw_h5_flash_ws_vos3(uint32_t hclk_hz)
{
    uint8_t ws = 0u;

    while (ws <= 4u) {
        if (hclk_hz <= 20000000uL * (uint32_t)(ws + 1u)) {
            return ws;
        }
        ws++;
    }
    return TFW_H5_FLASH_WS_NONE;
}

/* The IWDG's prescaler code and reload for a period: the smallest divider
 * (4 << code, codes 0 to 8) whose reload fits the 12-bit RLR, so the period
 * is as fine as the counter allows. 1 with both written, 0 if no divider
 * reaches the period from this LSI clock. */
#define TFW_H5_IWDG_PR_MAX 8u
#define TFW_H5_IWDG_RL_MAX 0x0FFFu

static inline uint8_t tfw_h5_iwdg_timing(uint32_t lsi_hz, uint32_t period_s,
                                         uint8_t *pr, uint16_t *rl)
{
    uint8_t code;

    for (code = 0u; code <= TFW_H5_IWDG_PR_MAX; code++) {
        uint64_t ticks = (uint64_t)lsi_hz * period_s / (4u << code);

        if (ticks >= 1u && ticks - 1u <= TFW_H5_IWDG_RL_MAX) {
            *pr = code;
            *rl = (uint16_t)(ticks - 1u);
            return 1u;
        }
    }
    return 0u;
}

/* The tone timer's prescaler and reload for a square wave of hz from a timer
 * clock of tim_hz (hal_tone.c). The channel toggles once per match, so a
 * period is two matches and a match is tim_hz / (2 * hz) timer clocks: the
 * prescaler divides by PSC + 1, the counter by ARR + 1. The prescaler taken is
 * the smallest that brings the count inside the 16-bit counter (finest
 * resolution), each step rounded to the nearest. 0, the outputs untouched,
 * for 0 Hz or a rate past half the clock (fewer than two counts a match). */
#define TFW_H5_TONE_COUNT_MAX 0x10000u   /* ARR + 1, a 16-bit counter */
#define TFW_H5_TONE_DIV_MAX   0x10000u   /* PSC + 1, a 16-bit prescaler */

static inline uint8_t tfw_h5_tone_psc_arr(uint32_t tim_hz, uint32_t hz,
                                          uint16_t *psc, uint16_t *arr)
{
    uint64_t twice;
    uint64_t match;
    uint64_t div;
    uint64_t count;

    if (hz == 0u) {
        return 0u;
    }
    twice = 2u * (uint64_t)hz;
    match = ((uint64_t)tim_hz + hz) / twice;
    div   = (match + TFW_H5_TONE_COUNT_MAX - 1u) / TFW_H5_TONE_COUNT_MAX;
    if (div == 0u) {
        div = 1u;
    }
    if (div > TFW_H5_TONE_DIV_MAX) {
        return 0u;
    }
    count = ((uint64_t)tim_hz + hz * div) / (twice * div);
    if (count < 2u || count > TFW_H5_TONE_COUNT_MAX) {
        return 0u;
    }
    *psc = (uint16_t)(div - 1u);
    *arr = (uint16_t)(count - 1u);
    return 1u;
}

/* A GPIO bank's base address from its index, 0 for GPIOA. The caller has
 * checked the index against TFW_H5_GPIO_BANKS. */
static inline uint32_t tfw_h5_gpio_base(uint32_t gpioa_base, uint32_t stride,
                                        uint8_t bank)
{
    return gpioa_base + stride * (uint32_t)bank;
}

/* `reg` with pin `pin`'s two-bit field (MODER, PUPDR, OSPEEDR) set to
 * `value`, every other pin's field kept. */
static inline uint32_t tfw_h5_field2(uint32_t reg, uint8_t pin, uint32_t value)
{
    uint32_t shift = 2u * (uint32_t)pin;

    return (reg & ~(0x3u << shift)) | ((value & 0x3u) << shift);
}

/* `reg`, an alternate-function register, with pin `pin`'s four-bit field set
 * to `af`. AFRL holds pins 0-7 and AFRH pins 8-15; either way the field is
 * the pin's index within its eight. */
static inline uint32_t tfw_h5_field4(uint32_t reg, uint8_t pin, uint32_t af)
{
    uint32_t shift = 4u * ((uint32_t)pin & 7u);

    return (reg & ~(0xFu << shift)) | ((af & 0xFu) << shift);
}

/* PWM on a general-purpose timer (hal_pwm.c): the counter runs 0 to ARR 254,
 * 255 steps, so duty 0 is CCR 0 (never high) and duty 255 is CCR 255, past
 * ARR (always high, PWM mode 1). The prescaler divides the timer's clock to
 * 255 kHz, the nearest a 16-bit PSC reaches, for about 1 kHz: what the AVR's
 * analogWrite and the ESP32's LEDC give. */
#define TFW_H5_PWM_ARR    254u
#define TFW_H5_PWM_HZ     1000u
#define TFW_H5_PWM_STEPS  (TFW_H5_PWM_ARR + 1u)

/* The PSC for about TFW_H5_PWM_HZ from `timer_hz`: the divider rounded to the
 * nearest, held to 1 to 0x10000, less one (the timer divides by PSC + 1). */
static inline uint16_t tfw_h5_pwm_psc(uint32_t timer_hz)
{
    const uint32_t tick = TFW_H5_PWM_STEPS * TFW_H5_PWM_HZ;
    uint32_t div = (uint32_t)(((uint64_t)timer_hz + tick / 2u) / tick);

    if (div == 0u) {
        div = 1u;
    }
    if (div > 0x10000u) {
        div = 0x10000u;
    }
    return (uint16_t)(div - 1u);
}

/* `reg`, CCMR1 (channels 1 and 2) or CCMR2 (3 and 4), with `channel`'s
 * output-compare half set to output (CCxS 00), preload on (OCxPE, bit 3 of
 * the half) and PWM mode 1 (OCxM 0110 at bits 6:4, OCxM_3 at bit 16 of
 * the register for the first channel, 24 for the second), the other half
 * kept. CMSIS TIM_CCMR1_OC1PE_Pos 3, OC1M_Pos 4, OC1M_3 0x00010000,
 * OC2PE_Pos 11, OC2M_Pos 12. */
#define TFW_H5_TIM_OCM_PWM1 0x6u

static inline uint32_t tfw_h5_pwm_ccmr(uint32_t reg, uint8_t channel)
{
    uint32_t shift = 8u * (((uint32_t)channel - 1u) & 1u);
    uint32_t mask  = (0xFFu << shift) | (0x1u << (16u + shift));
    uint32_t value = ((TFW_H5_TIM_OCM_PWM1 << 4) | (0x1u << 3)) << shift;

    return (reg & ~mask) | value;
}

/* `reg`, CCER, with `channel`'s output enabled (CCxE) or disabled, active
 * high (CCxP clear), the other channels kept: four bits a channel from
 * CC1E at 0 (CMSIS TIM_CCER_CC1E_Pos 0, CC2E_Pos 4). */
static inline uint32_t tfw_h5_pwm_ccer(uint32_t reg, uint8_t channel, uint8_t on)
{
    uint32_t shift = 4u * (((uint32_t)channel - 1u) & 3u);

    reg &= ~(0x3u << shift);
    return on ? (reg | (0x1u << shift)) : reg;
}

#endif /* TFW_CM_STM32H5_CALC_H */
