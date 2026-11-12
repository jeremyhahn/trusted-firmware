/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the MCXN94x HAL tests: every
 * register access goes to tfw_mcxn_model_reg, which the suite defines, so the
 * suite sees each access in order and answers the status reads a part would.
 * mk/host.mk puts this directory ahead of src/hal/cortex-m/ for those tests,
 * so soc_regs.h includes this file and hal_adc.c compiles unchanged.
 *
 * Only the register access is here: a SoC file that needs more of the real
 * cm_regs.h fails to compile against it, which is how a drift shows.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

volatile uint32_t *tfw_mcxn_model_reg(uintptr_t addr);

#define TFW_CM_REG(addr) (*tfw_mcxn_model_reg((uintptr_t)(addr)))

#endif /* TFW_CM_REGS_H */
