/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What the reset handler both images share (hal_early.c) gives the rest of
 * each image: the reset cause it latched, and the reset and watchdog
 * primitives that write the marker the next reset reads.
 *
 * A Cortex-M has no reset-cause register the architecture defines, and the
 * first SoC here (QEMU's mps2-tz) models none, so the cause is a marker word
 * in SRAM at a fixed address both images name (mk/arch/cortex-m.mk's
 * __tfw_resetmark, 4 bytes below the handoff block). Whoever causes a reset
 * writes it first:
 *
 *   TFW_CM_MARK_WATCHDOG  the watchdog was armed (the trial's, or the
 *                         application's): a reset with it set is the watchdog
 *   TFW_CM_MARK_RESETREQ  tfw_cm_system_reset asked for the reset
 *   anything else         nothing asked: power-on
 *
 * The reset handler reads it and clears it, so a value outlives one reset
 * only. Reset causes 5 (software) and 6 (panic) are not reported yet: the
 * request reads TFW_RESET_UNKNOWN.
 */
#ifndef TFW_CM_HAL_EARLY_H
#define TFW_CM_HAL_EARLY_H

#include <stdint.h>
#include "tfw/types.h"

#define TFW_CM_MARK_WATCHDOG 0x57444F47uL   /* 'G','O','D','W' little-endian */
#define TFW_CM_MARK_RESETREQ 0x52535251uL   /* 'Q','R','S','R' little-endian */

extern volatile uint32_t tfw_cm_reset_mark;

/* Why this image's reset handler was entered, as it read the marker. */
tfw_reset_cause_t tfw_hal_early_reset_cause(void);

/* Writes TFW_CM_MARK_RESETREQ and asks the core for a system reset
 * (AIRCR.SYSRESETREQ). Does not return. */
_Noreturn void tfw_cm_system_reset(void);

/* ---- the SoC's half, src/hal/cortex-m/<soc>/hal_soc.c, linked by both ---- */

/* Called by the reset handler before .data and .bss exist: disarms the
 * watchdog, which survives a reset on some parts and must not bite the image
 * that has only just started. Must touch no global. */
void tfw_cm_soc_early(void);

/* The longest period the SoC's watchdog has, armed, with
 * TFW_CM_MARK_WATCHDOG written first. */
void tfw_cm_soc_watchdog_arm(void);
void tfw_cm_soc_watchdog_pet(void);

#endif /* TFW_CM_HAL_EARLY_H */
