/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the nrf54l HAL tests: every
 * register access goes to a register model, test/model/nrf54l_model.h's for
 * the PWM test, the test's own for the TWIM test (test/unit/test_nrf54l_twim.c).
 * mk/host.mk puts this directory ahead of src/hal/cortex-m/ so soc_regs.h
 * includes this file and the SoC code compiles unchanged.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

#include "../../nrf54l_model.h"

volatile uint32_t *tfw_nrf54l_model_reg(uintptr_t addr);

#define TFW_CM_REG(addr) (*tfw_nrf54l_model_reg((uintptr_t)(addr)))

/* No barriers on the host: the model's registers are plain memory. */
static inline void tfw_cm_dsb(void) { }
static inline void tfw_cm_isb(void) { }

#endif /* TFW_CM_REGS_H */
