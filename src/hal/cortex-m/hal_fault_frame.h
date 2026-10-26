/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The fault record: what tfw_cm_fault (hal_early.c) keeps of the faulting
 * frame across the reset it takes. A debug aid, not a wire field: SYS_INFO
 * reports PANIC and nothing more, and the record is read from outside, by a
 * debugger or QEMU's gdbstub, at the fixed SRAM address both images name
 * (mk/arch/cortex-m.mk's __tfw_faultframe, 12 bytes below the reset marker).
 *
 *   word 0  TFW_CM_FAULT_PC    the stacked return address: the instruction
 *                              that faulted, or the one after it
 *   word 1  TFW_CM_FAULT_LR    the stacked LR
 *   word 2  TFW_CM_FAULT_XPSR  the stacked xPSR
 *
 * The words come from the basic frame the core pushes on exception entry,
 * r0-r3, r12, LR, the return address and xPSR (ARMv7-M ARM DDI 0403E, B1.5.6,
 * exception entry behavior; ARMv8-M ARM DDI 0553 keeps the same basic frame
 * for the state the exception was taken from). Both images run soft float,
 * so no FP state extends it, and on the AN505 everything runs Secure (#15 is
 * open), so no additional state precedes it.
 *
 * A frame that would sit outside [lo, hi), the image's SRAM up to its stack
 * top, is not read: reading a wild stack pointer would fault in the fault
 * handler and lock the core up. All three words then read
 * TFW_CM_FAULT_NOFRAME.
 *
 * Nothing clears the record: it is only meaningful in the boot whose reset
 * cause is PANIC, and holds the last fault's frame until the next one.
 *
 * Header-only and free of the core's registers, so a host test pins the
 * save (test/unit/test_cm_fault_frame.c).
 */
#ifndef TFW_HAL_CM_FAULT_FRAME_H
#define TFW_HAL_CM_FAULT_FRAME_H

#include <stdint.h>

#define TFW_CM_FAULT_PC      0u
#define TFW_CM_FAULT_LR      1u
#define TFW_CM_FAULT_XPSR    2u
#define TFW_CM_FAULT_WORDS   3u

#define TFW_CM_FAULT_NOFRAME 0xFFFFFFFFuL

/* The basic frame's words, from the stack pointer up. */
#define TFW_CM_FRAME_LR      5u
#define TFW_CM_FRAME_PC      6u
#define TFW_CM_FRAME_XPSR    7u
#define TFW_CM_FRAME_WORDS   8u

static inline void tfw_cm_fault_keep(volatile uint32_t *rec, uintptr_t sp,
                                     uintptr_t lo, uintptr_t hi)
{
    if ((sp & 3u) != 0u || sp < lo || sp > hi ||
        hi - sp < TFW_CM_FRAME_WORDS * sizeof(uint32_t)) {
        rec[TFW_CM_FAULT_PC]   = TFW_CM_FAULT_NOFRAME;
        rec[TFW_CM_FAULT_LR]   = TFW_CM_FAULT_NOFRAME;
        rec[TFW_CM_FAULT_XPSR] = TFW_CM_FAULT_NOFRAME;
        return;
    }
    const volatile uint32_t *frame = (const volatile uint32_t *)sp;
    rec[TFW_CM_FAULT_PC]   = frame[TFW_CM_FRAME_PC];
    rec[TFW_CM_FAULT_LR]   = frame[TFW_CM_FRAME_LR];
    rec[TFW_CM_FAULT_XPSR] = frame[TFW_CM_FRAME_XPSR];
}

#endif /* TFW_HAL_CM_FAULT_FRAME_H */
