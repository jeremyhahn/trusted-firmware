/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x's hal_soc.c for test/unit/test_mcxn_soc.c, compiled with
 * -DTFW_CM_MODEL_REGFILE and its TFW_CM_REG sent to model_soc_reg. Here, in
 * the register model's directory, because it is the register header's
 * substitute (mk/host.mk lint-arch-leak).
 *
 * The WWDT0 registers and the two dividers get a fresh word in model_wlog
 * per access, which keeps each write in order (a feed is two writes to one
 * register, and a divider is halted before its count goes in); every other
 * register is the register file's (regfile_model.c).
 */
#ifndef TFW_MCXN_SOC_MODEL_H
#define TFW_MCXN_SOC_MODEL_H

#include <stdint.h>

#include "unity.h"
#include "cm_regs.h"

static volatile uint32_t *model_soc_reg(uintptr_t addr);
#undef TFW_CM_REG
#define TFW_CM_REG(addr) (*model_soc_reg((uintptr_t)(addr)))

#include "../../../src/hal/cortex-m/mcxn94x/hal_soc.c"

/* hal_early.c's word in .resetmark, which the suite does not link. */
volatile uint32_t tfw_cm_reset_mark;
#define model_reset_mark tfw_cm_reset_mark

#define MODEL_WLOG_MAX 32u

static struct {
    uintptr_t         addr;
    volatile uint32_t val;
} model_wlog[MODEL_WLOG_MAX];
static unsigned model_wlog_n;

static int model_is_wwdt(uintptr_t addr)
{
    return addr >= (uintptr_t)TFW_MCXN_WWDT_BASE && addr < (uintptr_t)TFW_MCXN_WWDT_BASE + 0x1000u;
}

static int model_is_logged(uintptr_t addr)
{
    return model_is_wwdt(addr) || addr == (uintptr_t)TFW_MCXN_SYSCON_BASE + 0x380u ||
           addr == (uintptr_t)TFW_MCXN_SYSCON_BASE + 0x38Cu;
}

static volatile uint32_t *model_soc_reg(uintptr_t addr)
{
    if (!model_is_logged(addr)) {
        return tfw_cm_model_reg(addr);
    }
    TEST_ASSERT_TRUE_MESSAGE(model_wlog_n < MODEL_WLOG_MAX, "write log full");
    model_wlog[model_wlog_n].addr = addr;
    model_wlog[model_wlog_n].val  = 0u;
    return &model_wlog[model_wlog_n++].val;
}

static void model_soc_early(void)          { tfw_cm_soc_early(); }
static void model_soc_watchdog_arm(void)   { tfw_cm_soc_watchdog_arm(); }
static void model_soc_watchdog_pet(void)   { tfw_cm_soc_watchdog_pet(); }

#endif /* TFW_MCXN_SOC_MODEL_H */
