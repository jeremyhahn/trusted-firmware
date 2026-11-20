/*
 * MCXN94x register model for host unit tests (row mcxn947).
 *
 * A test that includes the MCXN94x HAL sources whole includes this header
 * first: it claims the target's cm_regs.h guard, so soc_regs.h's registers
 * resolve to the test's cm_slot() table instead of to the bus. The test
 * defines cm_slot().
 */
#ifndef TFW_MCXN_REG_MODEL_H
#define TFW_MCXN_REG_MODEL_H

#include <stdint.h>

static volatile uint32_t *cm_slot(uintptr_t addr);

#define TFW_CM_REGS_H
#define TFW_CM_REG(addr) (*cm_slot((uintptr_t)(addr)))

#endif /* TFW_MCXN_REG_MODEL_H */
