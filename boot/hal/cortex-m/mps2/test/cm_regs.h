/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's stand-in for src/hal/cortex-m/cm_regs.h on mps2: every register
 * access a SoC file makes goes through tfw_cm_model_reg, the hook the test
 * supplies, so a driver written for the part runs unchanged on the host and
 * the model sees each access as it happens. Put this directory on the include
 * path ahead of src/hal/cortex-m. Each access is one call: a model that
 * compares its registers with their last values on every call sees every
 * store the driver made by the next access, and can move its own state (a
 * transmitter draining) between the driver's polls. Only TFW_CM_REG is
 * modelled; a file that needs the SCB, SysTick, the NVIC, the interrupt mask
 * or a barrier fails to compile or link against it, which is the signal that
 * it needs a model of its own.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

/* The register at addr in the test's model. Never NULL: the model fails the
 * test on an address it cannot hold. */
volatile uint32_t *tfw_cm_model_reg(uint32_t addr);

#define TFW_CM_REG(addr) (*tfw_cm_model_reg((uint32_t)(addr)))

#define TFW_CM_RAMFUNC(name) __attribute__((noipa))

#endif /* TFW_CM_REGS_H */
