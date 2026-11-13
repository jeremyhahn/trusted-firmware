/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the nRF54L TWIM test
 * (test/unit/test_nrf54l_twim.c): TFW_CM_REG goes to the test's register
 * model. mk/host.mk puts this directory ahead of src/hal/cortex-m/ so the
 * SoC's soc_regs.h includes this file and hal_bus.c compiles unchanged.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

volatile uint32_t *tfw_nrf54l_model_reg(uintptr_t addr);

#define TFW_CM_REG(addr) (*tfw_nrf54l_model_reg((uintptr_t)(addr)))

static inline void tfw_cm_dsb(void) {}
static inline void tfw_cm_isb(void) {}

#endif /* TFW_CM_REGS_H */
