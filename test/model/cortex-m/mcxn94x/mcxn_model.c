/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#include <stdio.h>
#include <stdlib.h>

#include "mcxn_model.h"

#define MODEL_REGS 64u
#define MODEL_LOG  256u

static struct { uintptr_t addr; volatile uint32_t val; } regs[MODEL_REGS];
static size_t    nregs;
static struct { uintptr_t addr; uint32_t watched; } log_[MODEL_LOG];
static size_t    nlog;
static uintptr_t watched;
static int       watching;

static volatile uint32_t *slot(uintptr_t addr)
{
    size_t i;

    for (i = 0; i < nregs; i++) {
        if (regs[i].addr == addr) {
            return &regs[i].val;
        }
    }
    if (nregs == MODEL_REGS) {
        fprintf(stderr, "mcxn model: more than %u registers\n", MODEL_REGS);
        abort();
    }
    regs[nregs].addr = addr;
    regs[nregs].val  = 0u;
    return &regs[nregs++].val;
}

volatile uint32_t *tfw_mcxn_model_reg(uintptr_t addr)
{
    if (nlog == MODEL_LOG) {
        fprintf(stderr, "mcxn model: more than %u accesses\n", MODEL_LOG);
        abort();
    }
    log_[nlog].addr    = addr;
    log_[nlog].watched = watching ? *slot(watched) : 0u;
    nlog++;
    return slot(addr);
}

void tfw_mcxn_model_reset(void)
{
    nregs    = 0;
    nlog     = 0;
    watching = 0;
}

uint32_t tfw_mcxn_model_get(uintptr_t addr)
{
    return *slot(addr);
}

void tfw_mcxn_model_set(uintptr_t addr, uint32_t v)
{
    *slot(addr) = v;
}

void tfw_mcxn_model_watch(uintptr_t addr)
{
    watched  = addr;
    watching = 1;
}

size_t tfw_mcxn_model_log_len(void)
{
    return nlog;
}

uintptr_t tfw_mcxn_model_log_addr(size_t i)
{
    return log_[i].addr;
}

uint32_t tfw_mcxn_model_log_watched(size_t i)
{
    return log_[i].watched;
}

size_t tfw_mcxn_model_log_first(uintptr_t addr)
{
    size_t i;

    for (i = 0; i < nlog && log_[i].addr != addr; i++) {
    }
    return i;
}
