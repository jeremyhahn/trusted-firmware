/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The LPI2C master's clock arithmetic (lpi2c_timing.h): the SDK's
 * LPI2C_MasterSetBaudRate, FILTSCL 0, in 32-bit arithmetic.
 */
#include "lpi2c_timing.h"

/* SCL_LATENCY's numerator, 2 + FILTSCL, FILTSCL left at its reset value 0. */
#define LATENCY_NUM     2u
/* The SDK's ceiling on CLKLO + CLKHI before the latency: 1.92 x 63, for tBUF. */
#define CYCLE_CEILING   120u

tfw_status_t tfw_mcxn_lpi2c_timing(uint32_t clk_hz, uint32_t hz,
                                   tfw_mcxn_lpi2c_timing_t *out)
{
    uint32_t best_err = UINT32_MAX;
    uint32_t best_pre = 0u;
    uint32_t best_div = 1u;
    uint32_t best_cycle = 0u;
    uint32_t per;          /* functional clocks per SCL period, prescaled */
    uint32_t high;
    uint32_t low;
    uint32_t hold;
    uint32_t datavd;
    uint32_t filtsda = 0u;

    if (clk_hz == 0u || hz == 0u) {
        return TFW_ERR_RANGE;
    }
    for (uint32_t pre = 0u; pre <= TFW_MCXN_LPI2C_MCFGR1_PRESCALE_MAX; pre++) {
        uint32_t div = 1u << pre;
        uint32_t lat = LATENCY_NUM / div;
        uint32_t ticks = (uint32_t)(((uint64_t)clk_hz * 10u / div / hz + 5u) / 10u);
        uint32_t cycle;
        uint32_t got;
        uint32_t err;

        if (ticks < lat + 2u + 2u) {
            continue;          /* too fast for this prescale: no CLKLO, CLKHI */
        }
        cycle = ticks - lat - 2u;
        if (cycle > CYCLE_CEILING - lat) {
            continue;          /* too slow: the next prescale halves it */
        }
        got = (clk_hz / div) / (cycle + 2u + lat);
        err = (got > hz) ? got - hz : hz - got;
        if (err < best_err) {
            best_err = err;
            best_pre = pre;
            best_div = div;
            best_cycle = cycle;
            if (err == 0u) {
                break;
            }
        }
    }
    if (best_err == UINT32_MAX || best_err > hz / 20u) {
        return TFW_ERR_RANGE;
    }

    /* CLKHI no more than half the cycle after the latency (tLO >= tHI), and
     * low enough that CLKLO holds 0.52 of a period (tBUF). */
    high = (best_cycle - LATENCY_NUM / best_div) / 2u;
    per = clk_hz / hz / best_div;
    while (high > 0u && (uint64_t)high + (uint64_t)52u * clk_hz / hz / best_div / 100u >
                        (uint64_t)best_cycle + 1u) {
        high--;
    }
    low = best_cycle - high;
    /* SETHOLD: tHD:STA, tSU:STA and tSU:STO at half a period. DATAVD: tVD:DAT
     * at a quarter. */
    if (per / 4u == 0u) {
        return TFW_ERR_RANGE;
    }
    hold = per / 2u - 1u;
    datavd = per / 4u - 1u;
    if (high == 0u || low > TFW_MCXN_LPI2C_MCCR0_FIELD_MAX ||
        high > TFW_MCXN_LPI2C_MCCR0_FIELD_MAX || hold > TFW_MCXN_LPI2C_MCCR0_FIELD_MAX ||
        datavd > TFW_MCXN_LPI2C_MCCR0_FIELD_MAX) {
        return TFW_ERR_RANGE;
    }
    /* FILTSDA: tSU:DAT at 0.05 of a period, (2 + FILTSDA + 2^PRESCALE)
     * functional clocks. */
    if (clk_hz / hz / 20u > best_div + 2u) {
        filtsda = clk_hz / hz / 20u - best_div - 2u;
        if (filtsda > TFW_MCXN_LPI2C_MCFGR2_FILTSDA_MAX) {
            filtsda = TFW_MCXN_LPI2C_MCFGR2_FILTSDA_MAX;
        }
    }

    out->mcfgr1 = best_pre;
    out->mcfgr2 = filtsda << TFW_MCXN_LPI2C_MCFGR2_FILTSDA_SHIFT;
    out->mccr0 = (low << TFW_MCXN_LPI2C_MCCR0_CLKLO_SHIFT) |
                 (high << TFW_MCXN_LPI2C_MCCR0_CLKHI_SHIFT) |
                 (hold << TFW_MCXN_LPI2C_MCCR0_SETHOLD_SHIFT) |
                 (datavd << TFW_MCXN_LPI2C_MCCR0_DATAVD_SHIFT);
    return TFW_OK;
}
