/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7's ADC: the 12-bit SAR ADC the i.MX 93 also carries
 * (sar_adc.h), built only in the board's soft:ADC=1 row (TFW_CM_ADC).
 *
 * Polled, no interrupt: init powers the block up and calibrates it once;
 * each read is one normal-mode, one-shot conversion of the one channel the
 * pin table names. The HAL muxes no pin and programs no clock: the System
 * Manager owns both (board_profile.h). The reference is the block's own,
 * the default, as on boards without a selectable one.
 *
 * main() calls tfw_hal_adc_init on every board and boots whatever it says,
 * so a calibration that fails or never completes is recorded here, and every
 * read answers that status until an init succeeds.
 *
 * TFW_IMX95_ADC_BASE zero means unsupported: init succeeds, every read
 * answers TFW_ERR_UNSUPPORTED, and no register is touched.
 *
 * With the row off this is the stub it replaced: no channel
 * (TFW_ADC_COUNT 0), every read out of range, as on mps2-tz
 * (src/hal/cortex-m/mps2/hal_adc.c).
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"

#if TFW_CM_ADC

#include "sar_adc.h"

/* Polls, not time: the M7 has no timebase this early. Linux allows the
 * calibration two seconds; at the M7's clock a poll is well over a
 * nanosecond, so these bound the waits generously without hanging boot. */
#define ADC_POLL_POWER 100000uL
#define ADC_POLL_CAL   2000000uL
#define ADC_POLL_CONV  100000uL

#define ADC(off) TFW_IMX95_ADC_REG(TFW_IMX95_ADC_BASE, (off))

static tfw_status_t adc_state = TFW_ERR_UNSUPPORTED;  /* until init */

static tfw_status_t adc_calibrate(void)
{
    uint32_t n;

    /* Power down, as imx93_adc.c does before calibrating, and wait for it. */
    ADC(TFW_IMX95_ADC_MCR) |= TFW_IMX95_ADC_MCR_PWDN;
    for (n = 0u; n < ADC_POLL_POWER; n++) {
        if ((ADC(TFW_IMX95_ADC_MSR) & TFW_IMX95_ADC_MSR_ADCSTATUS) ==
            TFW_IMX95_ADC_STATUS_POWER_DOWN) {
            break;
        }
    }
    if (n == ADC_POLL_POWER) {
        return TFW_ERR_TIMEOUT;
    }
    /* The block's own divider: bus clock / 2 while calibrating. */
    ADC(TFW_IMX95_ADC_MCR) &= ~TFW_IMX95_ADC_MCR_ADCLKSE;
    ADC(TFW_IMX95_ADC_MCR) &= ~TFW_IMX95_ADC_MCR_PWDN;
    ADC(TFW_IMX95_ADC_MCR) |= TFW_IMX95_ADC_MCR_CALSTART;
    for (n = 0u; n < ADC_POLL_CAL; n++) {
        if (!(ADC(TFW_IMX95_ADC_MSR) & TFW_IMX95_ADC_MSR_CALBUSY)) {
            break;
        }
    }
    if (n == ADC_POLL_CAL) {
        ADC(TFW_IMX95_ADC_MCR) |= TFW_IMX95_ADC_MCR_PWDN;
        return TFW_ERR_TIMEOUT;
    }
    if (ADC(TFW_IMX95_ADC_MSR) & TFW_IMX95_ADC_MSR_CALFAIL) {
        ADC(TFW_IMX95_ADC_MCR) |= TFW_IMX95_ADC_MCR_PWDN;
        return TFW_ERR_INTERNAL;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_init(void)
{
    if (TFW_IMX95_ADC_BASE == 0u) {
        adc_state = TFW_ERR_UNSUPPORTED;
        return TFW_OK;
    }
    adc_state = adc_calibrate();
    return adc_state;
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    return ref == TFW_ADC_REF_VCC ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    uint32_t bit;
    uint32_t n;

    if (TFW_IMX95_ADC_BASE == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (channel >= TFW_ADC_COUNT || channel >= TFW_IMX95_ADC_CHANNELS) {
        return TFW_ERR_RANGE;
    }
    if (adc_state != TFW_OK) {
        return adc_state;
    }
    bit = 1u << channel;
    ADC(TFW_IMX95_ADC_NCMR0) = bit;
    ADC(TFW_IMX95_ADC_IMR) = 0u;                  /* polled */
    ADC(TFW_IMX95_ADC_CIMR0) = 0u;
    ADC(TFW_IMX95_ADC_ISR) = TFW_IMX95_ADC_ISR_EOC | TFW_IMX95_ADC_ISR_ECH;
    ADC(TFW_IMX95_ADC_CEOCFR0) = bit;
    ADC(TFW_IMX95_ADC_MCR) &= ~TFW_IMX95_ADC_MCR_MODE;   /* one-shot */
    ADC(TFW_IMX95_ADC_MCR) |= TFW_IMX95_ADC_MCR_NSTART;
    for (n = 0u; n < ADC_POLL_CONV; n++) {
        if (ADC(TFW_IMX95_ADC_ISR) & TFW_IMX95_ADC_ISR_EOC) {
            break;
        }
    }
    if (n == ADC_POLL_CONV) {
        ADC(TFW_IMX95_ADC_MCR) &= ~TFW_IMX95_ADC_MCR_NSTART;
        return TFW_ERR_TIMEOUT;
    }
    *out_raw = (uint16_t)(ADC(TFW_IMX95_ADC_PCDR(channel)) & TFW_IMX95_ADC_PCDR_CDATA);
    ADC(TFW_IMX95_ADC_ISR) = TFW_IMX95_ADC_ISR_EOC | TFW_IMX95_ADC_ISR_ECH;
    ADC(TFW_IMX95_ADC_CEOCFR0) = bit;
    return TFW_OK;
}

#else /* !TFW_CM_ADC: the stub */

tfw_status_t tfw_hal_adc_init(void)
{
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    (void)ref;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    (void)out_raw;
    if ((uint32_t)channel + 1u > TFW_ADC_COUNT) {     /* not >=: this board has none */
        return TFW_ERR_RANGE;
    }
    return TFW_ERR_UNSUPPORTED;
}

#endif /* TFW_CM_ADC */
