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
 * Init gives the converter its clock: §12.4.3 requires a 48 MHz clk_adc set
 * up before the ADC is enabled, so init brings PLL_USB up to 48 MHz from the
 * 12 MHz crystal and runs clk_adc from it undivided (clk_sys stays on the
 * crystal, hal_soc.c). It then takes
 * the block through reset, sets CS.EN and waits for CS.READY. A read hands
 * the pin's pad to the converter (digital input and output disabled, no
 * pulls, as §12.4 asks), selects the input in CS.AINSEL, starts one
 * conversion with CS.START_ONCE and polls CS.READY a bounded number of
 * times: past it the read is TFW_ERR_TIMEOUT. CS.ERR set is a failed
 * conversion, TFW_ERR_INTERNAL, never a value. Whatever the outcome, the
 * read then writes the pin's pad and IO_CTRL back as it found them, so a
 * pin another feature had configured keeps that configuration. The value is RESULT's 12
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

/* A conversion is 96 clk_adc cycles, 2 us at 48 MHz; with the core on the
 * 12 MHz crystal a poll costs more than a microsecond, so this bound is far
 * past any conversion and only ends a converter that never answers. */
#define ADC_POLL_MAX 10000u

/* PLL lock is microseconds to a millisecond or so; this bound, tens of
 * milliseconds of polls at 12 MHz, only ends a PLL that never locks. */
#define PLL_LOCK_POLL_MAX 100000u

static tfw_status_t wait_ready(void)
{
    for (uint32_t i = 0u; i < ADC_POLL_MAX; i++) {
        if ((TFW_RP_ADC_CS & TFW_RP_ADC_CS_READY) != 0u) {
            return TFW_OK;
        }
    }
    return TFW_ERR_TIMEOUT;
}

/* PLL_USB at 48 MHz, §8.6.4's sequence: out of reset, REFDIV and FBDIV,
 * power and VCO on, a bounded wait for CS.LOCK, then the post dividers and
 * their power. A PLL already locked at this setting is left running, as the
 * pico-sdk's pll_init does, so a second init does not reset it. */
static tfw_status_t pll_usb_48mhz(void)
{
    uint32_t i;

    if ((TFW_RP_PLL_USB_CS & TFW_RP_PLL_CS_LOCK) != 0u &&
        (TFW_RP_PLL_USB_CS & 0x3Fu) == TFW_RP_PLL_CS_REFDIV_1 &&
        (TFW_RP_PLL_USB_FBDIV & 0xFFFu) == TFW_RP_PLL_FBDIV_48MHZ &&
        (TFW_RP_PLL_USB_PRIM & TFW_RP_PLL_PRIM_MASK) == TFW_RP_PLL_PRIM_48MHZ) {
        return TFW_OK;
    }

    TFW_RP_SET(TFW_RP_RESETS_RESET) = TFW_RP_RESET_PLL_USB;
    TFW_RP_CLR(TFW_RP_RESETS_RESET) = TFW_RP_RESET_PLL_USB;
    for (i = 0u; i < ADC_POLL_MAX; i++) {
        if ((TFW_RP_RESETS_DONE & TFW_RP_RESET_PLL_USB) != 0u) {
            break;
        }
    }
    if (i == ADC_POLL_MAX) {
        return TFW_ERR_TIMEOUT;
    }

    TFW_RP_PLL_USB_CS    = TFW_RP_PLL_CS_REFDIV_1;
    TFW_RP_PLL_USB_FBDIV = TFW_RP_PLL_FBDIV_48MHZ;
    TFW_RP_CLR(TFW_RP_PLL_USB_PWR) = TFW_RP_PLL_PWR_PD | TFW_RP_PLL_PWR_VCOPD;
    for (i = 0u; i < PLL_LOCK_POLL_MAX; i++) {
        if ((TFW_RP_PLL_USB_CS & TFW_RP_PLL_CS_LOCK) != 0u) {
            break;
        }
    }
    if (i == PLL_LOCK_POLL_MAX) {
        return TFW_ERR_TIMEOUT;
    }

    TFW_RP_PLL_USB_PRIM = TFW_RP_PLL_PRIM_48MHZ;
    TFW_RP_CLR(TFW_RP_PLL_USB_PWR) = TFW_RP_PLL_PWR_POSTDIVPD;
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_init(void)
{
    uint32_t i;
    tfw_status_t st = pll_usb_48mhz();

    if (st != TFW_OK) {
        return st;
    }

    /* AUXSRC is not glitchless (§8.1.7 table 603): stop the generator, then
     * select PLL_USB undivided and start it. */
    TFW_RP_CLK_ADC_CTRL = 0u;
    TFW_RP_CLK_ADC_DIV  = TFW_RP_CLK_DIV_1;
    TFW_RP_CLK_ADC_CTRL = TFW_RP_CLK_ADC_ENABLE | TFW_RP_CLK_ADC_PLL_USB;

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
    uint32_t io_saved;
    uint32_t pad_saved;
    tfw_status_t st;

    if ((uint32_t)channel + 1u > TFW_ADC_COUNT) {     /* not >=: a board may have none */
        return TFW_ERR_RANGE;
    }

    io_saved = TFW_RP_IO_CTRL(gpio);
    pad_saved = TFW_CM_REG(TFW_RP_PADS_GPIO(gpio));
    TFW_RP_IO_CTRL(gpio) = TFW_RP_FUNC_NULL;
    TFW_CM_REG(TFW_RP_PADS_GPIO(gpio)) = TFW_RP_PAD_OD;   /* IE, pulls, ISO clear */

    TFW_RP_ADC_CS = TFW_RP_ADC_CS_EN | TFW_RP_ADC_CS_START_ONCE |
                    (((uint32_t)channel << TFW_RP_ADC_CS_AINSEL_LSB) & TFW_RP_ADC_CS_AINSEL_MASK);
    st = wait_ready();
    if (st == TFW_OK) {
        cs = TFW_RP_ADC_CS;
        if ((cs & TFW_RP_ADC_CS_ERR) != 0u) {
            st = TFW_ERR_INTERNAL;
        } else {
            *out_raw = (uint16_t)(TFW_RP_ADC_RESULT & TFW_RP_ADC_RESULT_MASK);
        }
    }

    /* the pin goes back as the read found it, on every outcome */
    TFW_CM_REG(TFW_RP_PADS_GPIO(gpio)) = pad_saved;
    TFW_RP_IO_CTRL(gpio) = io_saved;
    return st;
}
