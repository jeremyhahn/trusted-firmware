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

#endif /* TFW_CM_STM32H5_CALC_H */
