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

/* SysTick pends one interrupt however often it reaches 0 while interrupts are
 * masked, so a masked span longer than a millisecond would drop ticks. A
 * caller that masks them across a long ROM call measures the span on a
 * free-running counter and, before it unmasks, hands hal_tick.c the cycles and
 * SysTick's value when it masked: the millisecond count gains the reloads the
 * one pending interrupt does not count. The count is exact only if no tick
 * is pending when the caller reads SysTick on masking (it unmasks and reads
 * again while TFW_CM_ICSR_PENDSTSET shows one) and none comes due between its
 * last cycle reading and unmasking: tfw_cm_tick_wait_clear, just before that
 * reading, leaves SysTick far enough from a reload for both. */
void tfw_cm_tick_catch_up(uint32_t cvr_at_mask, uint32_t cycles);
void tfw_cm_tick_wait_clear(void);

#endif /* TFW_CM_CYCLES_H */
