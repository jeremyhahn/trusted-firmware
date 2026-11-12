/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The register seam of test/unit/test_mps2_tone.c: forced in ahead of the
 * driver (mk/host.mk, -include), it points src/hal/cortex-m/cm_regs.h's
 * TFW_CM_REG at the suite's model of TIMER0, the NVIC's enable words and the
 * FPGA I/O LED word, so src/hal/cortex-m/mps2/hal_tone.c compiles unchanged.
 * cm_regs.h keeps a TFW_CM_REG defined before it.
 */
#ifndef TFW_TEST_MPS2_TONE_MODEL_H
#define TFW_TEST_MPS2_TONE_MODEL_H

#include <stdint.h>

volatile uint32_t *tfw_mps2_tone_model_reg(uintptr_t addr);

#define TFW_CM_REG(addr) (*tfw_mps2_tone_model_reg((uintptr_t)(addr)))

/* One TIMER0 interrupt: the driver's service routine, named here so the
 * suite stays clear of the port's tfw_cm_* names (lint-arch-leak). */
void tfw_cm_irq3(void);
static inline void mps2_tone_model_edge(void) { tfw_cm_irq3(); }

#endif /* TFW_TEST_MPS2_TONE_MODEL_H */
