/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Analog read on LPADC0 (SPEC row mcxn947), one software-triggered 12-bit
 * single-ended conversion per read: command 1, trigger 0, FIFO 0. ADC1 is not
 * driven.
 *
 * The board table's adc_ch is the converter's input: bits 4:0 the channel
 * number, bit 5 the side (0 for A, 1 for B), which is CMDL's CTYPE field as
 * it stands, so channel A14 is 14 and B14 is 32 + 14. TFW_ADC_COUNT is 64,
 * the range of that encoding.
 *
 * Init follows the SDK's fsl_lpadc: the clock gate and reset release in
 * SYSCON0 as the other ports of this SoC do, FRO12M undivided as the
 * converter clock, a converter reset, both FIFOs reset, power on with the
 * SDK's default power-up delay and reference option 1, then the offset and
 * the gain calibration of both sides, every wait bounded. A calibration that
 * does not finish leaves the converter unused: init answers
 * TFW_ERR_TIMEOUT, which main() does not act on, and every read answers
 * TFW_ERR_UNSUPPORTED. So does every read after one that times out, until
 * init runs again, so that a late result is never taken for another
 * channel's.
 *
 * The result register holds a 12-bit conversion in bits 14:3 (the SDK's
 * lpadc examples shift it by 3); the read returns those 12 bits, the raw
 * width, as the ESP32 port returns its converter's 12 bits.
 *
 * Built, not run on the board: docs/boards/frdm-mcxn947.md lists what is not
 * confirmed.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"

#define ADC_POLL          100000u   /* bounded waits, in register reads */
#define ADC_CH_SIDE_B     0x20u     /* adc_ch bit 5: side B */
#define ADC_PUDLY         0x80u     /* the SDK's default power-up delay */
#define ADC_CAL_AVGS_128  7u
#define ADC_STS_131       7u        /* 131.5 ADCK cycles of sample time */
#define ADC_RESULT_SHIFT  3u        /* 12-bit result in D bits 14:3 */

static uint8_t ready;

static tfw_status_t adc_wait_cal_rdy(void)
{
    uint32_t n;

    for (n = 0u; n < ADC_POLL; n++) {
        if ((TFW_MCXN_ADC_STAT & TFW_MCXN_ADC_STAT_CAL_RDY) != 0u) {
            return TFW_OK;
        }
    }
    return TFW_ERR_TIMEOUT;
}

/* fsl_lpadc's LPADC_DoAutoCalibration, for side n: wait for the gain
 * calibration's result, then hand its correction back. */
static tfw_status_t adc_gain_side(uint32_t side)
{
    uint32_t n;
    uint32_t gcc;

    for (n = 0u; n < ADC_POLL; n++) {
        if ((TFW_MCXN_ADC_GCC(side) & TFW_MCXN_ADC_GCC_RDY) != 0u &&
            (TFW_MCXN_ADC_CTRL & TFW_MCXN_ADC_CTRL_CAL_REQ) == 0u) {
            break;
        }
    }
    if (n == ADC_POLL) {
        return TFW_ERR_TIMEOUT;
    }
    gcc = TFW_MCXN_ADC_GCC(side) & TFW_MCXN_ADC_GCC_GAIN_CAL;
    TFW_MCXN_ADC_GCR(side) = (uint16_t)((gcc << 16) / (0x1FFFFu - gcc));
    TFW_MCXN_ADC_GCR(side) |= TFW_MCXN_ADC_GCR_RDY;
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_init(void)
{
    tfw_status_t st;

    ready = 0u;
    TFW_MCXN_AHBCLKCTRLSET(1) = TFW_MCXN_CTRL1_ADC0;
    TFW_MCXN_PRESETCTRLCLR(1) = TFW_MCXN_CTRL1_ADC0;
    TFW_MCXN_ADC0CLKSEL = TFW_MCXN_ADCCLKSEL_FRO12M;
    TFW_MCXN_ADC0CLKDIV = TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT;
    TFW_MCXN_ADC0CLKDIV = 0u;                         /* divide by 1 */

    TFW_MCXN_ADC_CTRL = TFW_MCXN_ADC_CTRL_RST;
    TFW_MCXN_ADC_CTRL = 0u;
    TFW_MCXN_ADC_CTRL = TFW_MCXN_ADC_CTRL_RSTFIFO0 | TFW_MCXN_ADC_CTRL_RSTFIFO1;
    TFW_MCXN_ADC_CFG = TFW_MCXN_ADC_CFG_PWREN |
                       (ADC_PUDLY << TFW_MCXN_ADC_CFG_PUDLY_SHIFT) |
                       (0u << TFW_MCXN_ADC_CFG_REFSEL_SHIFT);
    TFW_MCXN_ADC_CTRL |= TFW_MCXN_ADC_CTRL_ADCEN;

    TFW_MCXN_ADC_CTRL |= TFW_MCXN_ADC_CTRL_CALOFS;    /* offset first */
    st = adc_wait_cal_rdy();
    if (st != TFW_OK) {
        return st;
    }
    TFW_MCXN_ADC_CTRL |= ADC_CAL_AVGS_128 << TFW_MCXN_ADC_CTRL_CAL_AVGS_SHIFT;
    TFW_MCXN_ADC_CTRL |= TFW_MCXN_ADC_CTRL_CAL_REQ;
    st = adc_gain_side(0u);
    if (st == TFW_OK) {
        st = adc_gain_side(1u);
    }
    if (st == TFW_OK) {
        st = adc_wait_cal_rdy();
    }
    if (st != TFW_OK) {
        return st;
    }
    ready = 1u;
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_set_ref(tfw_adc_ref_t ref)
{
    /* Reference option 1 only, taken as the board's 3.3 V rail (not
     * confirmed: docs/boards/frdm-mcxn947.md). */
    return ref == TFW_ADC_REF_VCC ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    uint32_t n;
    uint32_t cmdl;

    if ((uint32_t)channel + 1u > TFW_ADC_COUNT) {
        return TFW_ERR_RANGE;
    }
    if (!ready) {
        return TFW_ERR_UNSUPPORTED;
    }
    cmdl = channel & TFW_MCXN_ADC_CMDL_ADCH_MASK;
    if ((channel & ADC_CH_SIDE_B) != 0u) {
        cmdl |= TFW_MCXN_ADC_CMDL_CTYPE_B;
    }
    TFW_MCXN_ADC_CMDL1 = cmdl;
    TFW_MCXN_ADC_CMDH1 = ADC_STS_131 << TFW_MCXN_ADC_CMDH_STS_SHIFT;
    TFW_MCXN_ADC_TCTRL0 = 1u << TFW_MCXN_ADC_TCTRL_TCMD_SHIFT;
    TFW_MCXN_ADC_SWTRIG = TFW_MCXN_ADC_SWTRIG_SWT0;

    for (n = 0u; n < ADC_POLL; n++) {
        uint32_t r = TFW_MCXN_ADC_RESFIFO0;           /* a read pops it */
        if ((r & TFW_MCXN_ADC_RESFIFO_VALID) != 0u) {
            *out_raw = (uint16_t)((r & TFW_MCXN_ADC_RESFIFO_D) >> ADC_RESULT_SHIFT);
            return TFW_OK;
        }
    }
    /* The conversion may still finish and queue its result, which the next
     * read, of whatever channel, would pop as its own. Unmap the trigger,
     * drop FIFO 0, and refuse reads until init resets the converter: no
     * pinned file gives a way to abort a conversion in flight. */
    TFW_MCXN_ADC_TCTRL0 = 0u;
    TFW_MCXN_ADC_CTRL |= TFW_MCXN_ADC_CTRL_RSTFIFO0;
    ready = 0u;
    return TFW_ERR_TIMEOUT;
}
