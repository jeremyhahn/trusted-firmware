/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the MCXN94x HAL tests: every
 * register access goes to the sparse model (mcxn_model.h). mk/host.mk puts
 * this directory ahead of src/hal/cortex-m/, so soc_regs.h includes this file
 * and the SoC code compiles unchanged. Only the names the tested files use
 * are here; a name missing is a compile error in the host test.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

#include "mcxn_model.h"

#define TFW_CM_REG(addr) (*tfw_mcxn_model_reg((uintptr_t)(addr)))

#endif /* TFW_CM_REGS_H */
