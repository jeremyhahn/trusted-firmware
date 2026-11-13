/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <stdlib.h>

#include "nrf54l_model.h"

#define SLOTS 128u

static struct {
    uintptr_t         addr;
    volatile uint32_t val;
} slot[SLOTS];
static unsigned used;

volatile uint32_t *tfw_nrf54l_model_reg(uintptr_t addr)
{
    for (unsigned i = 0u; i < used; i++) {
        if (slot[i].addr == addr) {
            return &slot[i].val;
        }
    }
    if (used == SLOTS) {
        abort();
    }
    slot[used].addr = addr;
    slot[used].val  = 0u;
    return &slot[used++].val;
}

void tfw_nrf54l_model_reset(void)
{
    used = 0u;
}
