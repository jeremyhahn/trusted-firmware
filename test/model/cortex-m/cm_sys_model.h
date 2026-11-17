/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's stand-in for what src/hal/cortex-m/hal_sys.c calls below it:
 * the core's system reset, the SoC's watchdog and the reset handler's latch
 * (test/unit/hal/test_cortexm_sys.c). Each call is reported to the suite's
 * cm_sys_model_call() as one letter: 'R' the reset, 'A' the arm, 'p' a pet,
 * 'l' the latch read. The reset never returns: it longjmps to
 * cm_sys_model_reset.
 */
#ifndef TFW_CM_SYS_MODEL_H
#define TFW_CM_SYS_MODEL_H

#include <setjmp.h>

#include "tfw/types.h"

/* The suite's: logs one call. */
void cm_sys_model_call(char c);

extern jmp_buf           cm_sys_model_reset;
extern tfw_reset_cause_t cm_sys_model_latched;

#endif /* TFW_CM_SYS_MODEL_H */
