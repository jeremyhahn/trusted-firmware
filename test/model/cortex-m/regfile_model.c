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

/* test_nrf54l_soc.c fills FICR's 64-pair trim table (128 words): more than 32. */
#define REGFILE_WORDS 256u
static struct {
    uintptr_t addr;
    uint32_t  val;
} regfile[REGFILE_WORDS];
static unsigned regfile_used;

/* regfile_model_clear_after: from the n-th lookup of clear_addr on, the
 * word reads 0 (a status bit the part drops after a task). Off by default. */
static int clear_armed;
static uintptr_t clear_addr;
static unsigned clear_n;
static unsigned clear_seen;

volatile uint32_t *tfw_cm_model_reg(uintptr_t addr)
{
    unsigned i;

    for (i = 0u; i < regfile_used; i++) {
        if (regfile[i].addr == addr) {
            if (clear_armed && addr == clear_addr && ++clear_seen >= clear_n) {
                regfile[i].val = 0u;
            }
            return &regfile[i].val;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(regfile_used < REGFILE_WORDS, "register file full");
    regfile[regfile_used].addr = addr;
    if (clear_armed && addr == clear_addr && ++clear_seen >= clear_n) {
        regfile[regfile_used].val = 0u;
    }
    return &regfile[regfile_used++].val;
}

void regfile_model_clear_after(uintptr_t addr, unsigned n)
{
    clear_armed = 1;
    clear_addr = addr;
    clear_n = n;
    clear_seen = 0u;
}

void regfile_model_reset(void)
{
    memset(regfile, 0, sizeof(regfile));
    regfile_used = 0u;
    clear_armed = 0;
    clear_addr = 0u;
    clear_n = 0u;
    clear_seen = 0u;
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

void regfile_model_poke(uintptr_t addr, uint32_t val)
{
    *tfw_cm_model_reg(addr) = val;
}
