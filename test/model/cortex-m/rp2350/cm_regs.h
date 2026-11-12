/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the RP2350 HAL tests: the same
 * names, with every register access going to the RP2350 register model
 * (rp2350_model.h). mk/host.mk puts this directory ahead of
 * src/hal/cortex-m/, so soc_regs.h includes this file and the SoC code
 * compiles unchanged.
 *
 * Keep the names in step with the real file: a name missing here is a
 * compile error in the host test, which is how a drift shows.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

#include "rp2350_model.h"

#define TFW_CM_REG(addr) (*tfw_rp_model_reg((uintptr_t)(addr)))

#define TFW_CM_NVIC_ISER(n) TFW_CM_REG(0xE000E100u + 4u * (n))
#define TFW_CM_NVIC_ICER(n) TFW_CM_REG(0xE000E180u + 4u * (n))
#define TFW_CM_NVIC_ICPR(n) TFW_CM_REG(0xE000E280u + 4u * (n))

static inline void tfw_cm_nvic_enable(uint32_t irq)
{
    TFW_CM_NVIC_ICPR(irq / 32u) = 1u << (irq % 32u);
    TFW_CM_NVIC_ISER(irq / 32u) = 1u << (irq % 32u);
}

static inline void tfw_cm_nvic_disable(uint32_t irq)
{
    TFW_CM_NVIC_ICER(irq / 32u) = 1u << (irq % 32u);
    TFW_CM_NVIC_ICPR(irq / 32u) = 1u << (irq % 32u);
}

static inline void tfw_cm_dsb(void) {}
static inline void tfw_cm_isb(void) {}

#endif /* TFW_CM_REGS_H */
