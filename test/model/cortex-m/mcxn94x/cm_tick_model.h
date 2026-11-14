/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A model SysTick, DWT cycle counter and PRIMASK for
 * test/unit/test_mcxn94x_eeprom.c, in place of cm_regs.h, with the Cortex-M
 * tick (src/hal/cortex-m/hal_tick.c) compiled against it. Here, in the
 * register model's directory, because it is the register header's substitute
 * (mk/host.mk lint-arch-leak).
 *
 * model_run moves the processor clock on a cycle at a time, as the silicon
 * does: CYCCNT counts once DEMCR.TRCENA and DWT_CTRL.CYCCNTENA are set, and
 * SysTick counts down, reloads after 0 and, when it reaches 0 with TICKINT
 * set, takes its interrupt (SysTick_Handler) at once or, while PRIMASK masks
 * it, leaves one pending, however many times it reaches 0, until
 * tfw_cm_irq_restore unmasks it. Every register access and the unmask take
 * model_access cycles, so SysTick can come due between any two of them.
 */
#ifndef TFW_CM_TICK_MODEL_H
#define TFW_CM_TICK_MODEL_H

#include <stdint.h>

/* The real cm_regs.h stays out: these registers are the model's. */
#define TFW_CM_REGS_H

static uint32_t model_csr;
static uint32_t model_rvr;
static uint32_t model_cvr;
static uint32_t model_demcr;
static uint32_t model_dwt_ctrl;
static uint32_t model_cyccnt;
static uint32_t model_primask;
static unsigned model_pending;
static uint64_t model_now;      /* cycles since model_tick_reset */
static uint32_t model_access;   /* cycles each SysTick, DWT or ICSR access takes */

void SysTick_Handler(void);

/* cycles of the processor clock pass. */
static void model_run(uint32_t cycles)
{
    while (cycles-- != 0u) {
        model_now++;
        if ((model_demcr & (1u << 24)) != 0u && (model_dwt_ctrl & 0x1u) != 0u) {
            model_cyccnt++;
        }
        if ((model_csr & 0x1u) == 0u) {
            continue;
        }
        if (model_cvr == 0u) {
            model_cvr = model_rvr;
            continue;
        }
        if (--model_cvr == 0u && (model_csr & 0x2u) != 0u) {
            if (model_primask != 0u) {
                model_pending = 1u;
            } else {
                SysTick_Handler();
            }
        }
    }
}

/* A register access: its cycles pass first, then it reads or writes. */
static uint32_t *model_reg(uint32_t *reg)
{
    model_run(model_access);
    return reg;
}

#define TFW_CM_SYST_CSR           (*model_reg(&model_csr))
#define TFW_CM_SYST_RVR           (*model_reg(&model_rvr))
#define TFW_CM_SYST_CVR           (*model_reg(&model_cvr))
#define TFW_CM_SYST_CSR_ENABLE    0x1u
#define TFW_CM_SYST_CSR_TICKINT   0x2u
#define TFW_CM_SYST_CSR_CLKSOURCE 0x4u
#define TFW_CM_SYST_RVR_MAX       0x00FFFFFFu

#define TFW_CM_ICSR               (model_run(model_access), model_pending != 0u ? TFW_CM_ICSR_PENDSTSET : 0u)
#define TFW_CM_ICSR_PENDSTSET     0x04000000u

#define TFW_CM_DEMCR              (*model_reg(&model_demcr))
#define TFW_CM_DEMCR_TRCENA       (1u << 24)
#define TFW_CM_DWT_CTRL           (*model_reg(&model_dwt_ctrl))
#define TFW_CM_DWT_CTRL_CYCCNTENA 0x1u
#define TFW_CM_DWT_CYCCNT         (*model_reg(&model_cyccnt))

static inline void tfw_cm_dsb(void) { }
static inline void tfw_cm_isb(void) { }

static inline uint32_t tfw_cm_irq_save(void)
{
    uint32_t was = model_primask;

    model_primask = 1u;
    return was;
}

/* Unmasking takes its cycles too, still masked: a tick due in them pends. */
static inline void tfw_cm_irq_restore(uint32_t primask)
{
    model_run(model_access);
    model_primask = primask;
    if (primask == 0u && model_pending != 0u) {
        model_pending = 0u;
        SysTick_Handler();
    }
}

#include "../../../../src/hal/cortex-m/hal_tick.c"

#define MODEL_CYC_PER_MS (TFW_CM_TICK_RELOAD + 1u)

/* The model's power-on state: every counter stopped, interrupts unmasked. */
static void model_tick_reset(void)
{
    model_csr = model_rvr = model_cvr = 0u;
    model_demcr = model_dwt_ctrl = model_cyccnt = 0u;
    model_primask = 0u;
    model_pending = 0u;
    model_now = 0u;
    model_access = 4u;
}

#endif /* TFW_CM_TICK_MODEL_H */
