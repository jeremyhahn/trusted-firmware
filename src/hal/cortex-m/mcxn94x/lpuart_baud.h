/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The LPUART's BAUD register for a functional clock and a bit rate: pure
 * arithmetic, no register access, so the host tests it
 * (test/unit/test_mcxn_lpuart.c) and hal_uart.c writes what it returns.
 *
 * The LPUART samples each bit OSR + 1 times and divides the clock by SBR:
 * baud = clk / ((OSR + 1) * SBR), the oversampling ratio 4..32 and SBR
 * 1..8191 (MCXNx4x Reference Manual, LPUART chapter, section to confirm).
 * A ratio below 8 needs BOTHEDGE, sampling on both edges. The search is the
 * SDK's LPUART_Init: every ratio, SBR rounded, the smallest error winning and
 * the larger ratio winning a tie, and a rate more than 3% off refused.
 */
#ifndef TFW_CM_MCXN94X_LPUART_BAUD_H
#define TFW_CM_MCXN94X_LPUART_BAUD_H

#include <stdint.h>

#include "tfw/status.h"

#define TFW_MCXN_LPUART_BAUD_BOTHEDGE (1uL << 17)
#define TFW_MCXN_LPUART_BAUD_OSR_SHIFT 24u
#define TFW_MCXN_LPUART_BAUD_SBR_MAX  0x1FFFuL

/* Writes the BAUD register's value (OSR, BOTHEDGE, SBR; every other field 0)
 * to *reg and returns TFW_OK, or returns TFW_ERR_RANGE and leaves *reg alone
 * when the clock or the rate is 0 or no setting comes within 3%. */
tfw_status_t tfw_mcxn_lpuart_baud(uint32_t clk_hz, uint32_t baud, uint32_t *reg);

#endif /* TFW_CM_MCXN94X_LPUART_BAUD_H */
