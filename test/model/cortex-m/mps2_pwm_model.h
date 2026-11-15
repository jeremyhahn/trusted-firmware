/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stand-in for cm_regs.h that test/unit/test_mps2_pwm.c compiles the MPS2
 * software PWM (src/hal/cortex-m/mps2/hal_pwm.c) against: its guard is
 * defined here, so the real header drops out, TFW_CM_REG goes to the suite's
 * model_reg, and the NVIC calls are recorded. It lives under
 * test/model/cortex-m/, the one test directory lint-arch-leak lets name the
 * Cortex-M register layer. Include it before hal_pwm.c, once.
 */
#ifndef TFW_TEST_MPS2_PWM_MODEL_H
#define TFW_TEST_MPS2_PWM_MODEL_H

#include <stdint.h>

#define TFW_CM_REGS_H
static uint32_t s_dt[8];      /* Timer1Load .. Timer1BGLoad, offsets 0x00-0x1C */
static uint32_t s_scratch;
static uint32_t s_nvic_on;    /* the interrupt number enabled, or ~0 */
static uint32_t s_nvic_off;   /* the interrupt number disabled, or ~0 */
static volatile uint32_t *model_reg(uintptr_t addr);
#define TFW_CM_REG(addr) (*model_reg((uintptr_t)(addr)))
static inline void tfw_cm_nvic_enable(uint32_t irq)  { s_nvic_on = irq; }
static inline void tfw_cm_nvic_disable(uint32_t irq) { s_nvic_off = irq; }
static inline uint32_t tfw_cm_irq_save(void) { return 0u; }
static inline void tfw_cm_irq_restore(uint32_t primask) { (void)primask; }

#endif /* TFW_TEST_MPS2_PWM_MODEL_H */
