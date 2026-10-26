/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Decodes the reset marker word into the architecture-neutral
 * tfw_reset_cause_t.
 *
 * A Cortex-M has no reset-cause register the architecture defines, and the
 * first SoC here (QEMU's mps2) models none, so the cause is the marker word
 * hal_early.h describes: whoever causes a reset writes it first. This is the
 * only place that knows the marker values. The application's reset handler
 * and tfw-boot's both decode through it (hal_early.c), so a cause latched by
 * the application with no tfw-boot and one tfw-boot hands across are decoded
 * the same way.
 *
 *   TFW_CM_MARK_WATCHDOG  TFW_RESET_WATCHDOG: the watchdog was armed, and
 *                         nothing asked for a reset after it was
 *   TFW_CM_MARK_RESETREQ  TFW_RESET_SOFTWARE: tfw_cm_system_reset asked for it
 *                         (AIRCR.SYSRESETREQ), the "firmware asked" cause of
 *                         include/tfw/types.h
 *   TFW_CM_MARK_PANIC     TFW_RESET_PANIC: tfw_cm_fault, the fatal-error
 *                         handler, took it after a HardFault
 *   anything else         TFW_RESET_POWER_ON: nothing asked
 *
 * Header-only and free of the core's registers, so a host test pins the
 * table (test/unit/test_cm_reset_cause.c).
 */
#ifndef TFW_HAL_CM_RESET_DECODE_H
#define TFW_HAL_CM_RESET_DECODE_H

#include <stdint.h>
#include "tfw/types.h"

#define TFW_CM_MARK_WATCHDOG 0x57444F47uL   /* 'G','O','D','W' little-endian */
#define TFW_CM_MARK_RESETREQ 0x52535251uL   /* 'Q','R','S','R' little-endian */
#define TFW_CM_MARK_PANIC    0x43494E50uL   /* 'P','N','I','C' little-endian */

static inline tfw_reset_cause_t tfw_cm_reset_decode(uint32_t mark)
{
    switch (mark) {
    case TFW_CM_MARK_WATCHDOG: return TFW_RESET_WATCHDOG;
    case TFW_CM_MARK_RESETREQ: return TFW_RESET_SOFTWARE;
    case TFW_CM_MARK_PANIC:    return TFW_RESET_PANIC;
    default:                   return TFW_RESET_POWER_ON;
    }
}

#endif /* TFW_HAL_CM_RESET_DECODE_H */
