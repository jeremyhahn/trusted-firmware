/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Processor cycles counted on SysTick's current value, shared by
 * tfw_hal_delay_us (hal_tick.c) and the pulse timer (hal_pulse.c).
 *
 * SysTick counts TFW_CM_CPU_HZ down from TFW_CM_TICK_RELOAD to 0 and reloads,
 * once a millisecond (hal_tick.c). The cycles between two reads are the drop
 * from the last value to this one, or, when the counter has reloaded in
 * between, the drop to 0 plus the reload's span. Exact to a cycle so long as
 * the reads are under a millisecond apart.
 */
#ifndef TFW_CM_CYCLES_H
#define TFW_CM_CYCLES_H

#include <stdint.h>

#include "tfw/config.h"
#include "cm_regs.h"

#define TFW_CM_TICK_RELOAD   ((uint32_t)(TFW_CM_CPU_HZ / 1000uL) - 1u)
#define TFW_CM_CYCLES_PER_US ((uint32_t)(TFW_CM_CPU_HZ / 1000000uL))

/* Cycles since *last, the value of the previous read; stores this read's. */
static inline uint32_t tfw_cm_cycles_since(uint32_t *last)
{
    uint32_t now = TFW_CM_SYST_CVR;
    uint32_t was = *last;

    *last = now;
    return (now <= was) ? (was - now) : (was + TFW_CM_TICK_RELOAD + 1u - now);
}

#endif /* TFW_CM_CYCLES_H */
