/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's accessor to the Cortex-M fault record
 * (src/hal/cortex-m/hal_fault_frame.h). A test under test/unit/ reaches
 * Cortex-M code only through a name declared here; mk/host.mk puts this
 * directory on test_cm_fault_frame's include path. The record's word indices
 * (TFW_CM_FAULT_*) come through unchanged with the record's header.
 */
#ifndef TFW_FAULT_FRAME_MODEL_H
#define TFW_FAULT_FRAME_MODEL_H

#include <stdint.h>

#include "../../../src/hal/cortex-m/hal_fault_frame.h"

static inline void cm_model_fault_keep(volatile uint32_t *rec, uintptr_t sp,
                                       uintptr_t lo, uintptr_t hi)
{
    tfw_cm_fault_keep(rec, sp, lo, hi);
}

#endif /* TFW_FAULT_FRAME_MODEL_H */
