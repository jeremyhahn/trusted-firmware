/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What the reset handler both images share (hal_early.c) gives the rest of
 * each image: the reset cause it latched, and the reset and watchdog
 * primitives that write the marker the next reset reads.
 *
 * A Cortex-M has no reset-cause register the architecture defines, and the
 * first SoC here (QEMU's mps2) models none, so the cause is a marker word
 * in SRAM at a fixed address both images name (mk/arch/cortex-m.mk's
 * __tfw_resetmark, 4 bytes below the handoff block). Whoever causes a reset
 * writes it first:
 *
 *   TFW_CM_MARK_WATCHDOG  the watchdog was armed (the trial's, or the
 *                         application's): a reset with it set is the watchdog
 *   TFW_CM_MARK_RESETREQ  tfw_cm_system_reset asked for the reset
 *   TFW_CM_MARK_PANIC     tfw_cm_fault took it after a fault
 *   anything else         nothing asked: power-on
 *
 * The reset handler reads it and clears it, so a value outlives one reset
 * only, and hands it to the SoC's tfw_cm_soc_reset_cause. A SoC with no
 * reset-status register decodes the marker alone (tfw_cm_mark_decode, which is
 * hal_reset_decode.h's decode): the request reads TFW_RESET_SOFTWARE and the
 * fault TFW_RESET_PANIC. A SoC with one reads its register and takes the
 * marker only where the register cannot tell who asked (nrf54l's
 * soc_reset.c). The NMI, the watchdog's first expiry, still parks the core in
 * tfw_cm_unhandled, so the watchdog's second expiry resets it and its marker
 * stands.
 */
#ifndef TFW_CM_HAL_EARLY_H
#define TFW_CM_HAL_EARLY_H

#include <stdint.h>
#include "tfw/types.h"
#include "hal_reset_decode.h"

extern volatile uint32_t tfw_cm_reset_mark;

/* Why this image's reset handler was entered, as the SoC decoded it. */
tfw_reset_cause_t tfw_hal_early_reset_cause(void);

/* The marker's cause, for a SoC with no reset-status register. */
tfw_reset_cause_t tfw_cm_mark_decode(uint32_t mark);

/* Writes TFW_CM_MARK_RESETREQ and asks the core for a system reset
 * (AIRCR.SYSRESETREQ). Does not return. */
_Noreturn void tfw_cm_system_reset(void);

/* The fault vectors' handler: keeps the faulting frame's PC, LR and xPSR in
 * the fault record (hal_fault_frame.h), writes TFW_CM_MARK_PANIC and asks for
 * the same system reset. Does not return; not declared _Noreturn because it
 * is naked, its body asm alone. */
void tfw_cm_fault(void);

/* ---- the SoC's half, src/hal/cortex-m/<soc>/hal_soc.c, linked by both ---- */

/* Called by the reset handler before .data and .bss exist: disarms the
 * watchdog, which survives a reset on some parts and must not bite the image
 * that has only just started. Must touch no global. */
void tfw_cm_soc_early(void);

/* The longest period the SoC's watchdog has, armed, with
 * TFW_CM_MARK_WATCHDOG written first. */
void tfw_cm_soc_watchdog_arm(void);
void tfw_cm_soc_watchdog_pet(void);

/* Called once by the reset handler, after .data and .bss exist, with the
 * marker it read: why the part reset. A SoC with a reset-status register
 * reads and clears it here, so the next reset is not reported as this one. */
tfw_reset_cause_t tfw_cm_soc_reset_cause(uint32_t mark);

#endif /* TFW_CM_HAL_EARLY_H */
