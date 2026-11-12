/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350's SAR converter, one conversion per read ([RP-DS] §12.4; the
 * registers are soc_regs.h's ADC group). The board table's adc_ch is the
 * converter's input: ADC0 to ADC2 are GPIO26 to 28 on the RP2350A. ADC3,
 * GPIO29, is the radio's SPI clock and the VSYS divider on the Pico 2 W and
 * the temperature sensor (input 4) is not exposed, so the board offers
 * three channels (TFW_ADC_COUNT).
 *
 * Init gives the converter its clock (clk_adc from the 12 MHz crystal, the
 * only clock this port runs; the datasheet's figures are for 48 MHz, so a
 * conversion takes four times as long, docs/boards/rp2350-pico2w.md), takes
 * the block through reset, sets CS.EN and waits for CS.READY. A read hands
 * the pin's pad to the converter (digital input and output disabled, no
 * pulls, as §12.4 asks), selects the input in CS.AINSEL, starts one
 * conversion with CS.START_ONCE and polls CS.READY a bounded number of
 * times: past it the read is TFW_ERR_TIMEOUT. CS.ERR set is a failed
 * conversion, TFW_ERR_INTERNAL, never a value. The value is RESULT's 12
 * bits, as the ESP32 ports return theirs; the host reads the width from the
 * board.
 *
 * The one reference is the ADC_AVDD supply: TFW_ADC_REF_VCC is accepted,
 * every other reference refused.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"

#include "soc_regs.h"

/* A conversion is 96 clk_adc cycles; at the 12 MHz both the core and the
 * converter run, a poll costs more than one, so this bound is far past any
 * conversion and only ends a converter that never answers. */
#define ADC_POLL_MAX 10000u

static tfw_status_t wait_ready(void)
{
    for (uint32_t i = 0u; i < ADC_POLL_MAX; i++) {
        if ((TFW_RP_ADC_CS & TFW_RP_ADC_CS_READY) != 0u) {
            return TFW_OK;
        }
    }
    return TFW_ERR_TIMEOUT;
}

tfw_status_t tfw_hal_adc_init(void)
{
    uint32_t i;

    TFW_RP_CLK_ADC_DIV  = TFW_RP_CLK_DIV_1;
    TFW_RP_CLK_ADC_CTRL = TFW_RP_CLK_ADC_ENABLE | TFW_RP_CLK_ADC_XOSC;

    TFW_RP_SET(TFW_RP_RESETS_RESET) = TFW_RP_RESET_ADC;
    TFW_RP_CLR(TFW_RP_RESETS_RESET) = TFW_RP_RESET_ADC;
    for (i = 0u; i < ADC_POLL_MAX; i++) {
        if ((TFW_RP_RESETS_DONE & TFW_RP_RESET_ADC) != 0u) {
            break;
        }
    }
    if (i == ADC_POLL_MAX) {
        return TFW_ERR_TIMEOUT;
    }

    TFW_RP_ADC_CS = TFW_RP_ADC_CS_EN;
    return wait_ready();
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    return ref == TFW_ADC_REF_VCC ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    const uint32_t gpio = TFW_RP_ADC_GPIO0 + channel;
    uint32_t cs;
    tfw_status_t st;

    if ((uint32_t)channel + 1u > TFW_ADC_COUNT) {     /* not >=: a board may have none */
        return TFW_ERR_RANGE;
    }

    TFW_RP_IO_CTRL(gpio) = TFW_RP_FUNC_NULL;
    TFW_CM_REG(TFW_RP_PADS_GPIO(gpio)) = TFW_RP_PAD_OD;   /* IE, pulls, ISO clear */

    TFW_RP_ADC_CS = TFW_RP_ADC_CS_EN | TFW_RP_ADC_CS_START_ONCE |
                    (((uint32_t)channel << TFW_RP_ADC_CS_AINSEL_LSB) & TFW_RP_ADC_CS_AINSEL_MASK);
    st = wait_ready();
    if (st != TFW_OK) {
        return st;
    }
    cs = TFW_RP_ADC_CS;
    if ((cs & TFW_RP_ADC_CS_ERR) != 0u) {
        return TFW_ERR_INTERNAL;
    }
    *out_raw = (uint16_t)(TFW_RP_ADC_RESULT & TFW_RP_ADC_RESULT_MASK);
    return TFW_OK;
}
