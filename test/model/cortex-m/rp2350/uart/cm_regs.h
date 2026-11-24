/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the RP2350 application UART's
 * test (test/unit/test_rp2350_uart.c, #621): TFW_CM_REG goes to the test's
 * reg(), each address its own cell, and the NVIC is two stubs that count.
 * mk/host.mk puts this directory ahead of src/hal/cortex-m/, so soc_regs.h
 * includes this file and hal_uart.c compiles unchanged.
 *
 * Keep the names in step with the real file: a name missing here is a
 * compile error in the host test, which is how a drift shows.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

static volatile uint32_t *reg(uintptr_t addr);

#define TFW_CM_REG(addr) (*reg((uintptr_t)(addr)))

static uint32_t nvic_enabled;
static uint32_t nvic_disabled;
static uint32_t nvic_last_irq;

static inline void tfw_cm_nvic_enable(uint32_t irq)
{
    nvic_enabled++;
    nvic_last_irq = irq;
}

static inline void tfw_cm_nvic_disable(uint32_t irq)
{
    nvic_disabled++;
    nvic_last_irq = irq;
}

#endif /* TFW_CM_REGS_H */
