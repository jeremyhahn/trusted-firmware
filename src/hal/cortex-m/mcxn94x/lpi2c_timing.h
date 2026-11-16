/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The LPI2C master's clock registers for a functional clock and an SCL rate:
 * pure arithmetic, no register access, so the host tests it
 * (test/unit/test_mcxn_lpi2c.c) and hal_bus.c writes what it returns.
 *
 * The search is the SDK's LPI2C_MasterSetBaudRate (drivers/lpi2c/fsl_lpi2c.c
 * at the mcux-sdk commit docs/reference/README.md pins), with FILTSCL at its
 * reset value 0:
 *
 *   rate = (clk / 2^PRESCALE) / (CLKLO + 1 + CLKHI + 1 + SCL_LATENCY),
 *   SCL_LATENCY = (2 + FILTSCL) / 2^PRESCALE
 *
 * every prescale 0..7, CLKLO + CLKHI at most 120 - SCL_LATENCY, the smallest
 * error winning; then CLKHI split off for tBUF, SETHOLD at half a period,
 * DATAVD at a quarter, and FILTSDA raised for tSU:DAT, all as the SDK does.
 * Unlike the SDK it computes in 32 bits, not 8, so a slow rate cannot wrap,
 * and it refuses a rate it cannot time instead of writing what it found.
 */
#ifndef TFW_CM_MCXN94X_LPI2C_TIMING_H
#define TFW_CM_MCXN94X_LPI2C_TIMING_H

#include <stdint.h>

#include "tfw/status.h"

typedef struct {
    uint32_t mcfgr1;   /* PRESCALE; every other field 0 */
    uint32_t mcfgr2;   /* FILTSDA; every other field 0 */
    uint32_t mccr0;    /* CLKLO, CLKHI, SETHOLD, DATAVD */
} tfw_mcxn_lpi2c_timing_t;

/* The fields, MCXN947_cm33_core0.h's LPI2C_MCFGR1_PRESCALE,
 * LPI2C_MCFGR2_FILTSDA and LPI2C_MCCR0_* masks: PRESCALE three bits, FILTSDA
 * four, each MCCR0 field six. */
#define TFW_MCXN_LPI2C_MCFGR1_PRESCALE_MAX  7u
#define TFW_MCXN_LPI2C_MCFGR2_FILTSDA_SHIFT 24u
#define TFW_MCXN_LPI2C_MCFGR2_FILTSDA_MAX   0xFu
#define TFW_MCXN_LPI2C_MCCR0_CLKLO_SHIFT    0u
#define TFW_MCXN_LPI2C_MCCR0_CLKHI_SHIFT    8u
#define TFW_MCXN_LPI2C_MCCR0_SETHOLD_SHIFT  16u
#define TFW_MCXN_LPI2C_MCCR0_DATAVD_SHIFT   24u
#define TFW_MCXN_LPI2C_MCCR0_FIELD_MAX      0x3Fu

/* Fills *out and returns TFW_OK, or returns TFW_ERR_RANGE and leaves *out
 * alone when the clock or the rate is 0, or no setting comes within 5% of the
 * rate with every field inside its six bits. */
tfw_status_t tfw_mcxn_lpi2c_timing(uint32_t clk_hz, uint32_t hz,
                                   tfw_mcxn_lpi2c_timing_t *out);

#endif /* TFW_CM_MCXN94X_LPI2C_TIMING_H */
