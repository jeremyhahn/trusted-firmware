/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's stand-in for src/hal/cortex-m/cm_regs.h: every register access a
 * SoC file makes lands in a register model the test supplies, by address, so a
 * driver written for the part runs unchanged on the host. Put this directory
 * on the include path ahead of src/hal/cortex-m, and link a definition of
 * tfw_cm_model_reg. TFW_CM_REG is modelled, and the interrupt mask and the
 * barriers are functions a test that needs them defines (a test that does
 * not, links without them); a file that needs the SCB, SysTick or the NVIC
 * fails to compile against it, which is the signal that it needs a model of
 * its own. TFW_CM_RAMFUNC keeps only noipa: SRAM and flash are one address
 * space on the host.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

/* The register at addr in the test's model. Never NULL: the model fails the
 * test on an address it cannot hold. */
volatile uint32_t *tfw_cm_model_reg(uint32_t addr);

#define TFW_CM_REG(addr) (*tfw_cm_model_reg((uint32_t)(addr)))

uint32_t tfw_cm_irq_save(void);
void     tfw_cm_irq_restore(uint32_t primask);
void     tfw_cm_dsb(void);
void     tfw_cm_isb(void);

#define TFW_CM_RAMFUNC(name) __attribute__((noipa))

#endif /* TFW_CM_REGS_H */
