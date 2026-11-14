/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the nrf54l HAL tests: every
 * register access goes to the register model (test/model/nrf54l_model.h, or
 * the TWIM test's own model in test/unit/test_nrf54l_twim.c). mk/host.mk puts
 * this directory ahead of src/hal/cortex-m/ so soc_regs.h includes this file
 * and the SoC code (hal_bus.c, hal_pwm.c, ...) compiles unchanged. Only the
 * names the modelled files use are here; one missing is a compile error in
 * the test.
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
