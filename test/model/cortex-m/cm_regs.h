/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h: the same names, with every register
 * access going to the register model (test/model/imx95_model.h) and the
 * PRIMASK instructions to the model's mask. mk/host.mk puts this directory
 * ahead of src/hal/cortex-m/ for the i.MX 95 HAL tests, so soc_regs.h
 * includes this file and the SoC code compiles unchanged.
 *
 * Keep the names in step with the real file: a name missing here is a
 * compile error in the host test, which is how a drift shows.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

#ifdef TFW_CM_MODEL_REGFILE
/* A suite with its own register file (test/unit/test_stm32h5_pwm.c) builds
 * with -DTFW_CM_MODEL_REGFILE and defines these: no i.MX 95 model linked. */
volatile uint32_t *tfw_cm_model_reg(uintptr_t addr);
#define TFW_CM_REG(addr) (*tfw_cm_model_reg((uintptr_t)(addr)))
static inline void     tfw_imx95_model_irq_mask(uint32_t m) { (void)m; }
static inline uint32_t tfw_imx95_model_irq_save(void) { return 0u; }
static inline void     tfw_imx95_model_irq_restore(uint32_t m) { (void)m; }
#else
#include "../imx95_model.h"

#define TFW_CM_REG(addr) (*tfw_imx95_model_reg((uintptr_t)(addr)))
#endif

#define TFW_CM_VTOR  TFW_CM_REG(0xE000ED08u)
#define TFW_CM_AIRCR TFW_CM_REG(0xE000ED0Cu)
#define TFW_CM_AIRCR_VECTKEY     0x05FA0000u
#define TFW_CM_AIRCR_SYSRESETREQ 0x00000004u

#define TFW_CM_SYST_CSR TFW_CM_REG(0xE000E010u)
#define TFW_CM_SYST_RVR TFW_CM_REG(0xE000E014u)
#define TFW_CM_SYST_CVR TFW_CM_REG(0xE000E018u)
#define TFW_CM_SYST_CSR_ENABLE    0x1u
#define TFW_CM_SYST_CSR_TICKINT   0x2u
#define TFW_CM_SYST_CSR_CLKSOURCE 0x4u
#define TFW_CM_SYST_RVR_MAX       0x00FFFFFFu

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

static inline void tfw_cm_irq_on(void)  { tfw_imx95_model_irq_mask(0u); }
static inline void tfw_cm_irq_off(void) { tfw_imx95_model_irq_mask(1u); }

static inline uint32_t tfw_cm_irq_save(void) { return tfw_imx95_model_irq_save(); }

static inline void tfw_cm_irq_restore(uint32_t primask)
{
    tfw_imx95_model_irq_restore(primask);
}

#endif /* TFW_CM_REGS_H */
