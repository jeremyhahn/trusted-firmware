/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A model SysTick for test/unit/test_cm_pulse.c, in place of cm_regs.h, with
 * the Cortex-M pulse timer and tick (src/hal/cortex-m/hal_pulse.c and
 * hal_tick.c) compiled against it. Here, in the register model's directory,
 * because it is the register header's substitute (mk/host.mk lint-arch-leak).
 *
 * The counter runs down from the reload of the board's TFW_CM_CPU_HZ and moves
 * model_step cycles on every read of its current value, which is the only
 * thing that moves model time: model_now, in cycles since the model started.
 */
#ifndef TFW_CM_PULSE_MODEL_H
#define TFW_CM_PULSE_MODEL_H

#include <stdint.h>

/* The real cm_regs.h stays out: SysTick is the model's. */
#define TFW_CM_REGS_H

static uint64_t model_now;
static uint32_t model_step;
static uint32_t model_csr;
static uint32_t model_rvr;
static uint32_t model_cvr_value;

static uint32_t  model_cvr_read(void);
static uint32_t *model_cvr_ref(void);

#define TFW_CM_SYST_CVR           (*model_cvr_ref())
#define TFW_CM_SYST_CSR           model_csr
#define TFW_CM_SYST_RVR           model_rvr
#define TFW_CM_SYST_CSR_ENABLE    0x1u
#define TFW_CM_SYST_CSR_TICKINT   0x2u
#define TFW_CM_SYST_CSR_CLKSOURCE 0x4u
#define TFW_CM_SYST_RVR_MAX       0x00FFFFFFu

#include "../../../src/hal/cortex-m/hal_pulse.c"
#include "../../../src/hal/cortex-m/hal_tick.c"

#define MODEL_CYC_PER_US  TFW_CM_CYCLES_PER_US
#define MODEL_RELOAD      TFW_CM_TICK_RELOAD
#define MODEL_RELOAD_SPAN (TFW_CM_TICK_RELOAD + 1u)

static uint32_t model_cvr_read(void)
{
    model_now += model_step;
    return MODEL_RELOAD - (uint32_t)(model_now % MODEL_RELOAD_SPAN);
}

/* A read steps the clock; a write lands in a scratch word. */
static uint32_t *model_cvr_ref(void)
{
    model_cvr_value = model_cvr_read();
    return &model_cvr_value;
}

/* The shared cycle step, cm_cycles.h's, under the model's name. */
static uint32_t model_cycles_since(uint32_t *last)
{
    return tfw_cm_cycles_since(last);
}

#endif /* TFW_CM_PULSE_MODEL_H */
