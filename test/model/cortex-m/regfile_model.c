/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The register file behind cm_regs.h's TFW_CM_REG when a host test builds
 * with -DTFW_CM_MODEL_REGFILE (regfile_model.h).
 */
#include <string.h>

#include "unity.h"
#include "cm_regs.h"
#include "regfile_model.h"

#define REGFILE_WORDS 32u
static struct {
    uintptr_t addr;
    uint32_t  val;
} regfile[REGFILE_WORDS];
static unsigned regfile_used;

volatile uint32_t *tfw_cm_model_reg(uintptr_t addr)
{
    unsigned i;

    for (i = 0u; i < regfile_used; i++) {
        if (regfile[i].addr == addr) {
            return &regfile[i].val;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(regfile_used < REGFILE_WORDS, "register file full");
    regfile[regfile_used].addr = addr;
    return &regfile[regfile_used++].val;
}

void regfile_model_reset(void)
{
    memset(regfile, 0, sizeof(regfile));
    regfile_used = 0u;
}

uint32_t regfile_model_peek(uintptr_t addr)
{
    unsigned i;

    for (i = 0u; i < regfile_used; i++) {
        if (regfile[i].addr == addr) {
            return regfile[i].val;
        }
    }
    return 0u;
}
