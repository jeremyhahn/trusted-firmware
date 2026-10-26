/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's accessor to the Cortex-M reset marker's decode
 * (src/hal/cortex-m/hal_reset_decode.h). A test under test/unit/ reaches
 * Cortex-M code only through a name declared here; mk/host.mk puts this
 * directory on test_cm_reset_cause's include path. The marker words
 * (TFW_CM_MARK_*) come through unchanged with the decode's header.
 */
#ifndef TFW_RESET_DECODE_MODEL_H
#define TFW_RESET_DECODE_MODEL_H

#include <stdint.h>

#include "tfw/types.h"
#include "../../../src/hal/cortex-m/hal_reset_decode.h"

static inline tfw_reset_cause_t cm_model_reset_decode(uint32_t mark)
{
    return tfw_cm_reset_decode(mark);
}

#endif /* TFW_RESET_DECODE_MODEL_H */
