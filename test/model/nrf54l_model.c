/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <stdlib.h>
#include <string.h>

#include "nrf54l_model.h"

#define SLOTS 128u

static struct {
    uintptr_t         addr;
    volatile uint32_t val;
} slot[SLOTS];
static unsigned used;

static struct {
    volatile uint32_t *task, *event, *blocked;
    unsigned           reads, left, hits;
} late;

static volatile uint32_t *word(uintptr_t addr)
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

volatile uint32_t *tfw_nrf54l_model_reg(uintptr_t addr)
{
    volatile uint32_t *w = word(addr);

    if (late.task != NULL && *late.task != 0u) {
        if (w == late.event) {
            if (late.left == 0u) {
                *late.event = 1u;
                *late.task  = 0u;
                late.left   = late.reads;
            } else {
                late.left--;
            }
        } else if (w == late.blocked) {
            late.hits++;
        }
    }
    return w;
}

void tfw_nrf54l_model_reset(void)
{
    used = 0u;
    memset(&late, 0, sizeof late);
}

void tfw_nrf54l_model_late_task(volatile uint32_t *task, volatile uint32_t *event,
                                unsigned reads, volatile uint32_t *blocked)
{
    late.task    = task;
    late.event   = event;
    late.blocked = blocked;
    late.reads   = reads;
    late.left    = reads;
    late.hits    = 0u;
}

unsigned tfw_nrf54l_model_blocked(void)
{
    return late.hits;
}
