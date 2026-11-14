/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The millisecond tick on SysTick, which every Cortex-M has, counting the
 * processor clock: TFW_CM_CPU_HZ / 1000 cycles is exactly 1.000 ms on the
 * first part (20 000 at 20 MHz), a true millisecond as on AVR.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "cm_regs.h"
#include "cm_cycles.h"

#define TICK_RELOAD   TFW_CM_TICK_RELOAD
#define CYCLES_PER_US TFW_CM_CYCLES_PER_US

_Static_assert(TICK_RELOAD <= TFW_CM_SYST_RVR_MAX,
               "SysTick's reload is 24 bits: TFW_CM_CPU_HZ / 1000 must fit");
_Static_assert(CYCLES_PER_US >= 1u, "tfw_hal_delay_us counts whole cycles per microsecond");

tfw_status_t tfw_hal_tick_init(void);
void         SysTick_Handler(void);

static volatile tfw_ms_t ticks;

tfw_status_t tfw_hal_tick_init(void)
{
    TFW_CM_SYST_CSR = 0u;
    TFW_CM_SYST_RVR = TICK_RELOAD;
    TFW_CM_SYST_CVR = 0u;
    TFW_CM_SYST_CSR = TFW_CM_SYST_CSR_CLKSOURCE | TFW_CM_SYST_CSR_TICKINT |
                      TFW_CM_SYST_CSR_ENABLE;
    return TFW_OK;
}

void SysTick_Handler(void)
{
    ticks++;
}

/* Called with interrupts masked (cm_cycles.h). From cvr_at_mask the counter
 * reaches 0 after cvr_at_mask cycles (a full reload span when it was at 0),
 * then every reload span after: each of those is a tick, and the one the
 * pending interrupt counts on unmask is left to it. */
void tfw_cm_tick_catch_up(uint32_t cvr_at_mask, uint32_t cycles)
{
    uint32_t first = (cvr_at_mask == 0u) ? TICK_RELOAD + 1u : cvr_at_mask;

    if ((TFW_CM_SYST_CSR & TFW_CM_SYST_CSR_ENABLE) == 0u || cycles <= first) {
        return;
    }
    ticks += (cycles - first) / (TICK_RELOAD + 1u);
}

/* An eighth of a millisecond either side of a reload: far more than the few
 * instructions from here to a caller's unmask, and to its last reading. */
#define TICK_GUARD ((TICK_RELOAD + 1u) / 8u)

void tfw_cm_tick_wait_clear(void)
{
    uint32_t cvr;

    if ((TFW_CM_SYST_CSR & TFW_CM_SYST_CSR_ENABLE) == 0u) {
        return;
    }
    do {
        cvr = TFW_CM_SYST_CVR;
    } while (cvr <= TICK_GUARD || cvr >= TICK_RELOAD - TICK_GUARD);
}

/* A 32-bit load is one instruction here, so the read needs no critical
 * section: it cannot catch the ISR's increment half done. */
tfw_ms_t tfw_hal_millis(void)
{
    return ticks;
}

/* Bus bit timing only (include/tfw/hal.h): counts SysTick's current value
 * down across reloads, so it is exact to a cycle and independent of the
 * optimiser. Needs tfw_hal_tick_init, which main() calls first. */
void tfw_hal_delay_us(uint16_t us)
{
    uint32_t want = (uint32_t)us * CYCLES_PER_US;
    uint32_t done = 0u;
    uint32_t last = TFW_CM_SYST_CVR;

    while (done < want) {
        done += tfw_cm_cycles_since(&last);
    }
}
