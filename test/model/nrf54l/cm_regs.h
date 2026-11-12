/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the nrf54l HAL tests: every
 * register access goes to the register model (test/model/nrf54l_model.h).
 * mk/host.mk puts this directory ahead of src/hal/cortex-m/ so soc_regs.h
 * includes this file and the SoC code compiles unchanged. Only the names the
 * modelled files use are here; one missing is a compile error in the test.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

#include "../nrf54l_model.h"

#define TFW_CM_REG(addr) (*tfw_nrf54l_model_reg((uintptr_t)(addr)))

#endif /* TFW_CM_REGS_H */
