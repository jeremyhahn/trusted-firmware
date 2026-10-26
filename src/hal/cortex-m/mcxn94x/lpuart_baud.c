/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <stdint.h>

#include "lpuart_baud.h"

#define OSR_MIN 4u
#define OSR_MAX 32u
#define OSR_BOTHEDGE_BELOW 8u

tfw_status_t tfw_mcxn_lpuart_baud(uint32_t clk_hz, uint32_t baud, uint32_t *reg)
{
    uint32_t best_diff = UINT32_MAX;
    uint32_t best_osr = 0u;
    uint32_t best_sbr = 0u;
    uint32_t v;

    if (clk_hz == 0u || baud == 0u) {
        return TFW_ERR_RANGE;
    }
    for (uint32_t osr = OSR_MIN; osr <= OSR_MAX; osr++) {
        uint64_t div = (uint64_t)osr * baud;
        uint64_t sbr = ((uint64_t)clk_hz + div / 2u) / div;
        uint32_t got;
        uint32_t diff;

        if (sbr == 0u) {
            sbr = 1u;
        }
        if (sbr > TFW_MCXN_LPUART_BAUD_SBR_MAX) {
            sbr = TFW_MCXN_LPUART_BAUD_SBR_MAX;
        }
        got = (uint32_t)(clk_hz / ((uint64_t)osr * sbr));
        diff = (got > baud) ? got - baud : baud - got;
        if (diff <= best_diff) {
            best_diff = diff;
            best_osr = osr;
            best_sbr = (uint32_t)sbr;
        }
    }
    if (best_diff > (baud / 100u) * 3u) {
        return TFW_ERR_RANGE;
    }
    v = ((best_osr - 1u) << TFW_MCXN_LPUART_BAUD_OSR_SHIFT) | best_sbr;
    if (best_osr < OSR_BOTHEDGE_BELOW) {
        v |= TFW_MCXN_LPUART_BAUD_BOTHEDGE;
    }
    *reg = v;
    return TFW_OK;
}
