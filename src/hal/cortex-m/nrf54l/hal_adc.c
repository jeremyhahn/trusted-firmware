/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Analog read on the nRF54L15's SAADC: one single-ended conversion per read,
 * polled, at 12 bits, on SAADC channel 0 pointed at the board channel's pin
 * (soc_tables.c, tfw_nrf54l_saadc_pin). Each read is self-contained:
 * enable, configure, START, SAMPLE, wait for END, STOP, disable and
 * disconnect the pin, so nothing is left driving or loading it between
 * reads. The registers are soc_regs.h's, from the nrfx MDK.
 *
 * The reference: the part has the internal 0.9 V reference and an external
 * one (nrf54l15_types.h, SAADC_CH_CONFIG_REFSEL), no VDD-based one, so
 * TFW_ADC_REF_VCC is refused; the external reference's pin is not on the
 * board's table, so TFW_ADC_REF_EXTERNAL is refused too. The gain is 2/8,
 * the widest range the GAIN field offers. The acquisition and conversion
 * times are CH[n].CONFIG's reset values, and no offset calibration is run:
 * docs/boards/nrf54l15-dk.md, "What is not verified".
 *
 * The raw value is the converter's 12 bits, as on the ESP32 ports; the host
 * reads the width from the board.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_tables.h"

/* The bound on each wait, in polls: the SAADC answers START, SAMPLE and
 * STOP in microseconds, so a wait this long is a peripheral that is not
 * running. */
#define SAADC_WAIT_POLLS 100000uL

/* EasyDMA's destination: one 16-bit sample, two bytes (RESULT.MAXCNT). */
static volatile int16_t saadc_sample;

tfw_status_t tfw_hal_adc_init(void)
{
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    return (ref == TFW_ADC_REF_INTERNAL) ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

static uint8_t saadc_wait(uint32_t event)
{
    uint32_t n;

    for (n = 0u; n < SAADC_WAIT_POLLS; n++) {
        if (TFW_CM_REG(event) != 0u) {
            return 1u;
        }
    }
    return 0u;
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    uint8_t port = 0u;
    uint8_t bit  = 0u;
    uint8_t ok;

    if ((uint32_t)channel + 1u > TFW_ADC_COUNT ||
        !tfw_nrf54l_saadc_pin(channel, &port, &bit)) {
        return TFW_ERR_RANGE;
    }

    TFW_NRF_SAADC_ENABLE      = TFW_NRF_SAADC_ENABLE_ON;
    TFW_NRF_SAADC_RESOLUTION  = TFW_NRF_SAADC_RESOLUTION_12;
    TFW_NRF_SAADC_OVERSAMPLE  = 0u;   /* Bypass */
    TFW_NRF_SAADC_SAMPLERATE  = 0u;   /* MODE Task: SAMPLE starts each conversion */
    TFW_NRF_SAADC_CH_PSELP(0) = tfw_nrf54l_saadc_pselp(port, bit);
    TFW_NRF_SAADC_CH_PSELN(0) = TFW_NRF_SAADC_PSEL_NC;
    TFW_NRF_SAADC_CH_CONFIG(0) = (TFW_NRF_SAADC_CONFIG_RESET &
                                  ~(7uL << TFW_NRF_SAADC_CONFIG_GAIN_POS)) |
                                 (TFW_NRF_SAADC_CONFIG_GAIN2_8 << TFW_NRF_SAADC_CONFIG_GAIN_POS) |
                                 (TFW_NRF_SAADC_CONFIG_REFSEL_INT << 12) |
                                 (TFW_NRF_SAADC_CONFIG_MODE_SE << 15);
    TFW_NRF_SAADC_RESULT_PTR    = (uint32_t)(uintptr_t)&saadc_sample;
    TFW_NRF_SAADC_RESULT_MAXCNT = (uint32_t)sizeof(saadc_sample);

    TFW_NRF_SAADC_EV_STARTED = 0u;
    TFW_NRF_SAADC_EV_END     = 0u;
    TFW_NRF_SAADC_EV_STOPPED = 0u;
    TFW_NRF_SAADC_START = 1u;
    ok = saadc_wait(TFW_NRF_SAADC_EV_STARTED_A);
    if (ok) {
        TFW_NRF_SAADC_SAMPLE = 1u;
        ok = saadc_wait(TFW_NRF_SAADC_EV_END_A);
    }
    TFW_NRF_SAADC_STOP = 1u;
    (void)saadc_wait(TFW_NRF_SAADC_EV_STOPPED_A);

    TFW_NRF_SAADC_ENABLE      = 0u;
    TFW_NRF_SAADC_CH_PSELP(0) = TFW_NRF_SAADC_PSEL_NC;

    if (!ok) {
        return TFW_ERR_TIMEOUT;
    }
    *out_raw = tfw_nrf54l_saadc_raw(saadc_sample);
    return TFW_OK;
}
