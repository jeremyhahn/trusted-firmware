/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the MCXN94x HAL tests: every
 * register access goes to tfw_mcxn_model_reg. A suite that links the sparse
 * model (mcxn_model.c) gets it from there; a suite that does not defines it
 * itself, so it sees each access in order and answers the status reads a
 * part would. mk/host.mk puts this directory ahead of src/hal/cortex-m/, so
 * soc_regs.h includes this file and the SoC code compiles unchanged. Only
 * the names the tested files use are here; a name missing is a compile error
 * in the host test, which is how a drift shows.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

#include "mcxn_model.h"

#define TFW_CM_REG(addr) (*tfw_mcxn_model_reg((uintptr_t)(addr)))

#endif /* TFW_CM_REGS_H */
