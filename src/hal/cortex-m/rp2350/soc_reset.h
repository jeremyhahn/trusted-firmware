/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The rp2350 SoC's reset cause: WATCHDOG.REASON and POWMAN.CHIP_RESET read
 * and decoded to the HAL's values. Both registers are reached through
 * pointers the caller names (hal_soc.c passes &TFW_RP_WATCHDOG_REASON and
 * &TFW_RP_POWMAN_CHIP_RESET), so a host test can hand them fake words
 * (test/unit/test_rp2350_reset.c).
 */
#ifndef TFW_CM_RP2350_SOC_RESET_H
#define TFW_CM_RP2350_SOC_RESET_H

#include <stdint.h>

#include "tfw/types.h"

/* The cause the two values record. The watchdog's timer alone in REASON is
 * the watchdog. Otherwise, when the reset handler found TFW_CM_MARK_RESETREQ
 * (src/hal/cortex-m/hal_early.h) and REASON is empty, the firmware's own
 * restart. Otherwise one HAD_ flag alone in CHIP_RESET that the HAL has a
 * value for is that cause; anything else is TFW_RESET_UNKNOWN. */
tfw_reset_cause_t tfw_rp2350_reset_decode(uint32_t reason, uint32_t chip_reset,
                                          uint8_t requested);

/* Reads both registers and returns their cause. Neither is cleared: each
 * records the last reset of its kind only (soc_reset.c). */
tfw_reset_cause_t tfw_rp2350_reset_take(const volatile uint32_t *reason,
                                        const volatile uint32_t *chip_reset,
                                        uint8_t requested);

#endif /* TFW_CM_RP2350_SOC_RESET_H */
