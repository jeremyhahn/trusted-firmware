/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * One-shot conversions on ADC1, polled. The board table's adc_ch is the
 * ADC1 input number (INPx), single-ended; TFW_ADC_COUNT is one past the
 * highest the board uses.
 *
 * The sequence is ST's (stm32h5xx_hal_adc.c HAL_ADC_Init and ADC_Enable,
 * stm32h5xx_hal_adc_ex.c HAL_ADCEx_Calibration_Start), every register and
 * bit soc_regs.h's, the arithmetic h5_calc.h's:
 *
 *   init   ADC bus clock on, kernel clock HCLK (no PLL, TFW_CM_CPU_HZ
 *          stays the core's), common clock HCLK / 2; out of deep power-down,
 *          regulator on and its start-up waited; INP0's pin switch on;
 *          single-ended calibration; ADEN, then ADRDY. 12 bits, right
 *          aligned, single conversion, an overrun overwrites.
 *   read   the channel's board pins back to analog, no pull (MODER 0b11,
 *          PUPDR 0b00); one conversion of the channel (SQR1), the longest sample time
 *          (a high-impedance source settles), ADSTART, then EOC; DR's 12
 *          bits are the raw count, as esp32's are.
 *
 * Every poll is bounded (TFW_ERR_TIMEOUT past it). main() ignores init's
 * status, so a converter that did not come up answers every read with
 * TFW_ERR_UNSUPPORTED, as esp32's does.
 *
 * The reference is VREF+, which TFW_ADC_REF_VCC names. The internal
 * reference buffer (VREFBUF) is not wired on this port, and the external
 * one is VREF+ itself, so both are refused.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/board.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "h5_calc.h"

/* CR's set-only bits: writing 0 to one leaves it, so a write carries only
 * the one being set (LL_ADC's ADC_CR_BITS_PROPERTY_RS, of the five used). */
#define ADC_CR_RS (TFW_H5_ADC_CR_ADCAL | TFW_H5_ADC_CR_ADSTP | TFW_H5_ADC_CR_ADSTART | \
                   TFW_H5_ADC_CR_ADDIS | TFW_H5_ADC_CR_ADEN)

#if TFW_ADC_COUNT > TFW_H5_ADC_CHANNELS
#error "TFW_ADC_COUNT is past ADC1's inputs"
#endif

static uint8_t ready;

static void spin(uint32_t n)
{
    for (volatile uint32_t i = 0u; i < n; i++) { }
}

static void cr_set(uint32_t bit)
{
    TFW_H5_ADC1_CR = (TFW_H5_ADC1_CR & ~ADC_CR_RS) | bit;
}

/* Polls CR or ISR until `mask` reads `want`; 1 if it did inside the bound. */
static uint8_t wait_cr(uint32_t mask, uint32_t want)
{
    uint32_t n = tfw_h5_spins_us(TFW_CM_CPU_HZ, TFW_H5_ADC_POLL_US);

    while (n-- != 0u) {
        if ((TFW_H5_ADC1_CR & mask) == want) {
            return 1u;
        }
    }
    return 0u;
}

static uint8_t wait_isr(uint32_t flag)
{
    uint32_t n = tfw_h5_spins_us(TFW_CM_CPU_HZ, TFW_H5_ADC_POLL_US);

    while (n-- != 0u) {
        if ((TFW_H5_ADC1_ISR & flag) != 0u) {
            return 1u;
        }
    }
    return 0u;
}

tfw_status_t tfw_hal_adc_init(void)
{
    ready = 0u;
    TFW_H5_RCC_CCIPR5 = (TFW_H5_RCC_CCIPR5 & ~TFW_H5_RCC_CCIPR5_ADCDACSEL) |
                        TFW_H5_RCC_ADCDACSEL_HCLK;
    TFW_H5_RCC_AHB2ENR |= TFW_H5_RCC_AHB2ENR_ADCEN;
    (void)TFW_H5_RCC_AHB2ENR;                      /* the enable lands first */

    TFW_H5_ADC12_CCR = (TFW_H5_ADC12_CCR & ~(TFW_H5_ADC_CCR_CKMODE | TFW_H5_ADC_CCR_PRESC)) |
                       (TFW_H5_ADC_PRESC_CODE << TFW_H5_ADC_CCR_PRESC_POS);

    TFW_H5_ADC1_CR = 0u;                           /* DEEPPWD off */
    TFW_H5_ADC1_CR = TFW_H5_ADC_CR_ADVREGEN;
    spin(tfw_h5_spins_us(TFW_CM_CPU_HZ, TFW_H5_ADC_REGUL_US));

    TFW_H5_ADC1_OR |= TFW_H5_ADC_OR_OP0;           /* INP0 reaches PA0 */
    TFW_H5_ADC1_DIFSEL = 0u;                       /* every input single-ended */

    TFW_H5_ADC1_CR = (TFW_H5_ADC1_CR & ~(ADC_CR_RS | TFW_H5_ADC_CR_ADCALDIF)) |
                     TFW_H5_ADC_CR_ADCAL;
    if (!wait_cr(TFW_H5_ADC_CR_ADCAL, 0u)) {
        return TFW_ERR_TIMEOUT;
    }
    spin(tfw_h5_adc_cal_en_spins());

    TFW_H5_ADC1_ISR = TFW_H5_ADC_ISR_ADRDY;
    cr_set(TFW_H5_ADC_CR_ADEN);
    if (!wait_isr(TFW_H5_ADC_ISR_ADRDY)) {
        return TFW_ERR_TIMEOUT;
    }

    TFW_H5_ADC1_CFGR = (TFW_H5_ADC1_CFGR & ~(TFW_H5_ADC_CFGR_RES | TFW_H5_ADC_CFGR_ALIGN |
                                             TFW_H5_ADC_CFGR_CONT)) |
                       TFW_H5_ADC_CFGR_OVRMOD;
    ready = 1u;
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    return (ref == TFW_ADC_REF_VCC) ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

/* Puts every board pin wired to `channel` back in analog mode, no pull, as
 * reset leaves it: the GPIO commands reach these pins too (they check the
 * reserved and in-use flags, not TFW_PIN_DIGITAL), and an output or a
 * pull-up left on one would drive or bias the input being sampled. The bank
 * is the GPIO HAL's, from the pin's board coordinate. */
static void analog_pins(uint8_t channel)
{
    for (tfw_pin_t p = 0u; p < TFW_PIN_COUNT; p++) {
        tfw_pinref_t ref;
        uint8_t      ch;
        uint32_t     bank;

        if (tfw_board_adc_channel(p, &ch) != TFW_OK || ch != channel ||
            tfw_board_pin(p, &ref) != TFW_OK ||
            ref.port >= TFW_H5_GPIO_BANKS || ref.bit >= TFW_H5_GPIO_PINS) {
            continue;
        }
        bank = tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, ref.port);
        TFW_H5_RCC_AHB2ENR |= 1u << ref.port;
        (void)TFW_H5_RCC_AHB2ENR;               /* the enable lands before the bank is written */
        TFW_H5_GPIO_MODER(bank) = tfw_h5_field2(TFW_H5_GPIO_MODER(bank), ref.bit,
                                                TFW_H5_MODE_ANALOG);
        TFW_H5_GPIO_PUPDR(bank) = tfw_h5_field2(TFW_H5_GPIO_PUPDR(bank), ref.bit,
                                                TFW_H5_PULL_NONE);
    }
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    if ((uint32_t)channel + 1u > TFW_ADC_COUNT) {     /* not >=: a board may have none */
        return TFW_ERR_RANGE;
    }
    if (!ready) {
        return TFW_ERR_UNSUPPORTED;
    }
    analog_pins(channel);
    if (channel < 10u) {
        TFW_H5_ADC1_SMPR1 = tfw_h5_adc_smpr(TFW_H5_ADC1_SMPR1, channel, TFW_H5_ADC_SMP_640C5);
    } else {
        TFW_H5_ADC1_SMPR2 = tfw_h5_adc_smpr(TFW_H5_ADC1_SMPR2, channel, TFW_H5_ADC_SMP_640C5);
    }
    TFW_H5_ADC1_SQR1 = (uint32_t)channel << TFW_H5_ADC_SQR1_SQ1_POS;   /* L 0: one */
    TFW_H5_ADC1_ISR  = TFW_H5_ADC_ISR_EOC | TFW_H5_ADC_ISR_EOS | TFW_H5_ADC_ISR_OVR;
    cr_set(TFW_H5_ADC_CR_ADSTART);
    if (!wait_isr(TFW_H5_ADC_ISR_EOC)) {
        /* Stop the hung conversion so it cannot answer the next read; if it
         * will not stop, refuse reads until the next init. */
        cr_set(TFW_H5_ADC_CR_ADSTP);
        if (!wait_cr(TFW_H5_ADC_CR_ADSTART, 0u)) {
            ready = 0u;
        }
        TFW_H5_ADC1_ISR = TFW_H5_ADC_ISR_EOC | TFW_H5_ADC_ISR_EOS | TFW_H5_ADC_ISR_OVR;
        return TFW_ERR_TIMEOUT;
    }
    *out_raw = (uint16_t)(TFW_H5_ADC1_DR & TFW_H5_ADC_DR_12BIT);   /* reading DR clears EOC */
    return TFW_OK;
}
