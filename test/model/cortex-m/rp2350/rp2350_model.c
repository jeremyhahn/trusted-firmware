/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The flat RP2350 register model behind test/model/cortex-m/rp2350/cm_regs.h.
 * See rp2350_model.h.
 */
#include <stdio.h>
#include <stdlib.h>

#include "rp2350_model.h"

#define MODEL_CELLS 128u

static uintptr_t         model_addr[MODEL_CELLS];
static volatile uint32_t model_val[MODEL_CELLS];
static unsigned          model_used;

volatile uint32_t *tfw_rp_model_reg(uintptr_t addr)
{
    for (unsigned i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return &model_val[i];
        }
    }
    if (model_used == MODEL_CELLS) {
        fprintf(stderr, "rp2350 model: out of cells\n");
        abort();
    }
    model_addr[model_used] = addr;
    model_val[model_used]  = 0u;
    return &model_val[model_used++];
}

void tfw_rp_model_reset(void)
{
    model_used = 0u;
}

uint32_t tfw_rp_model_peek(uintptr_t addr)
{
    return *tfw_rp_model_reg(addr);
}

void tfw_rp_model_poke(uintptr_t addr, uint32_t value)
{
    *tfw_rp_model_reg(addr) = value;
}
