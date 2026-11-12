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

#define TICK_RELOAD ((uint32_t)(TFW_CM_CPU_HZ / 1000uL) - 1u)
#define CYCLES_PER_US ((uint32_t)(TFW_CM_CPU_HZ / 1000000uL))

_Static_assert(TICK_RELOAD <= TFW_CM_SYST_RVR_MAX,
               "SysTick's reload is 24 bits: TFW_CM_CPU_HZ / 1000 must fit");
_Static_assert(CYCLES_PER_US >= 1u, "tfw_hal_delay_us counts whole cycles per microsecond");

tfw_status_t tfw_hal_tick_init(void);
void         SysTick_Handler(void);
void         tfw_cm_tick_credit(tfw_ms_t ms);

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

/* Milliseconds SysTick counted but the handler could not: expirations while
 * interrupts are masked collapse into the one interrupt pending at the
 * unmask, so a caller that masks across a longer operation (the RP2350's
 * flash EEPROM write) credits the rest from its own timer. Called with
 * interrupts masked, so the handler's increment cannot fall between this
 * load and store. */
void tfw_cm_tick_credit(tfw_ms_t ms)
{
    ticks += ms;
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
        uint32_t now = TFW_CM_SYST_CVR;

        done += (now <= last) ? (last - now) : (last + TICK_RELOAD + 1u - now);
        last  = now;
    }
}
