/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's cm_regs.h for test/unit/test_stm32h5_ee_flash_port.c: included
 * ahead of src/hal/cortex-m/stm32h5/ee_flash_port.c, it takes cm_regs.h's
 * guard, so the port compiles unchanged with its DSB and PRIMASK pair going to
 * the test's mask model instead of the instructions. The test defines the
 * model (stub_*).
 */
#ifndef STM32H5_EE_PORT_STUBS_H
#define STM32H5_EE_PORT_STUBS_H

#define TFW_CM_REGS_H

#include <stdint.h>

/* No register is reached through these: the port takes its addresses as
 * plain constants. A use is a link error in the test. */
volatile uint32_t *stub_no_cm_reg(uintptr_t addr);
#define TFW_CM_REG(addr) (*stub_no_cm_reg((uintptr_t)(addr)))

extern uint32_t stub_primask;   /* the model's PRIMASK */
extern unsigned stub_dsbs;      /* DSBs so far */
extern unsigned stub_saves;     /* tfw_cm_irq_save calls */
extern unsigned stub_restores;  /* tfw_cm_irq_restore calls */
extern unsigned stub_dsbs_at_restore;

static inline void tfw_cm_dsb(void) { stub_dsbs++; }

static inline uint32_t tfw_cm_irq_save(void)
{
    uint32_t m = stub_primask;

    stub_saves++;
    stub_primask = 1u;
    return m;
}

static inline void tfw_cm_irq_restore(uint32_t primask)
{
    stub_restores++;
    stub_dsbs_at_restore = stub_dsbs;
    stub_primask = primask;
}

#endif /* STM32H5_EE_PORT_STUBS_H */
