/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 port's arithmetic and tables (soc_calc.h). No register is
 * touched here: the M7 image and the host tests link this one file.
 *
 * Only 32-bit division is used, and 64-bit only in products, so the M7 build
 * needs no 64-bit division routine from libgcc.
 */

#include <stdint.h>

#include "soc_calc.h"

/* RGPIO banks GPIO1 to GPIO5 (TO CONFIRM against NXP IMX95RM, the memory map:
 * these are the addresses NXP's i.MX 95 device tree in Linux gives them). */
static const uint32_t gpio_bases[TFW_IMX95_GPIO_PORTS] = {
    0x47400000u,   /* GPIO1, in the AON domain */
    0x43810000u,   /* GPIO2 */
    0x43820000u,   /* GPIO3 */
    0x43840000u,   /* GPIO4 */
    0x43850000u,   /* GPIO5 */
};

uint32_t tfw_imx95_gpio_base(uint8_t port)
{
    if (port >= TFW_IMX95_GPIO_PORTS) {
        return 0u;
    }
    return gpio_bases[port];
}

static uint32_t abs_diff(uint32_t a, uint32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

tfw_status_t tfw_imx95_lpuart_baud(uint32_t clk_hz, uint32_t baud, uint32_t *baud_reg)
{
    uint32_t osr;
    uint32_t best_reg = 0u;
    uint32_t best_err = 0xFFFFFFFFu;

    if (clk_hz == 0u || baud == 0u) {
        return TFW_ERR_RANGE;
    }
    for (osr = TFW_IMX95_LPUART_OSR_MIN; osr <= TFW_IMX95_LPUART_OSR_MAX; osr++) {
        uint32_t d, sbr, rem, actual, err;

        if (baud > 0xFFFFFFFFu / osr) {
            continue;                   /* the divisor would be below 1 */
        }
        d   = baud * osr;
        sbr = clk_hz / d;
        rem = clk_hz % d;
        if (rem >= d - rem) {
            sbr++;                      /* to the nearest, without clk + d/2 */
        }
        if (sbr == 0u || sbr > TFW_IMX95_LPUART_SBR_MAX) {
            continue;
        }
        actual = clk_hz / (osr * sbr);
        err    = abs_diff(actual, baud);
        if (err <= best_err) {          /* <=: of equal errors, the higher ratio */
            best_err = err;
            best_reg = ((osr - 1u) << TFW_IMX95_LPUART_BAUD_OSR_SHIFT) | sbr;
            if (osr < 8u) {
                best_reg |= TFW_IMX95_LPUART_BAUD_BOTHEDGE;
            }
        }
    }
    if (best_err == 0xFFFFFFFFu ||
        (uint64_t)best_err * 1000u > (uint64_t)baud * TFW_IMX95_LPUART_ERR_PPT_MAX) {
        return TFW_ERR_RANGE;
    }
    *baud_reg = best_reg;
    return TFW_OK;
}
