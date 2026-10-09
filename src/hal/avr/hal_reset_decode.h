/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Decodes a raw MCUSR byte into the architecture-neutral tfw_reset_cause_t.
 *
 * This is the ONLY place in the tree that knows MCUSR's bit positions. The
 * application's HAL and tfw-boot's HAL both reach it through hal_early.c, so a
 * reset observed in .init3 and a cause handed across by tfw-boot are decoded by
 * one priority order rather than by two that can drift. Everything above the
 * HAL -- core, the handoff, the update-request gate, SYS_INFO -- sees only the
 * enum.
 *
 * Header-only and free of <avr/io.h> so a host test can pin the priority order.
 * The bit positions below are pinned against avr-libc by _Static_asserts in
 * hal_early.c, the same arrangement each board's board_profile.h uses for
 * register addresses.
 *
 * Priority, most specific first: BORF, EXTRF, PORF. WDRF names a watchdog reset
 * ONLY when it is the sole cause bit. MCUSR accumulates, so WDRF beside another
 * flag means something other than the application's own tfw_hal_reset() caused
 * (or also caused) this restart, and the update-request gate must not read that
 * as "the application asked for it". A mixed reset therefore reports the other
 * cause -- a brownout in particular names a power problem, which is more useful
 * than a watchdog it may well have provoked.
 */
#ifndef TFW_HAL_AVR_RESET_DECODE_H
#define TFW_HAL_AVR_RESET_DECODE_H

#include <stdint.h>
#include "tfw/types.h"

#define TFW_MCUSR_PORF  0x01u
#define TFW_MCUSR_EXTRF 0x02u
#define TFW_MCUSR_BORF  0x04u
#define TFW_MCUSR_WDRF  0x08u

/*
 * Written as assignments in REVERSE priority (the last one that applies wins)
 * rather than an if-chain of returns: on AVR each becomes one skip-if-bit
 * -clear and one load-immediate, with a single narrowing at the end, where
 * five separate returns of a 16-bit enum cost a load pair apiece. This runs in
 * both images and the 32U4 application has almost no flash to spare.
 */
static inline tfw_reset_cause_t tfw_avr_reset_decode(uint8_t mcusr)
{
    uint8_t cause = (uint8_t)TFW_RESET_UNKNOWN;

    if (mcusr & TFW_MCUSR_WDRF)  { cause = (uint8_t)TFW_RESET_WATCHDOG; }
    if (mcusr & TFW_MCUSR_PORF)  { cause = (uint8_t)TFW_RESET_POWER_ON; }
    if (mcusr & TFW_MCUSR_EXTRF) { cause = (uint8_t)TFW_RESET_EXTERNAL; }
    if (mcusr & TFW_MCUSR_BORF)  { cause = (uint8_t)TFW_RESET_BROWNOUT; }
    return (tfw_reset_cause_t)cause;
}

#endif /* TFW_HAL_AVR_RESET_DECODE_H */
