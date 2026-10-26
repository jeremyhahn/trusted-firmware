/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "cm_regs.h"
#include "hal_early.h"
#include "../../core/handoff.h"

extern uint32_t __heap_start;

/* The clocks are the SoC's at reset on the first part: QEMU's mps2 runs
 * the core and the APB at TFW_CM_CPU_HZ with nothing to program. A part with
 * a clock tree sets it up in its SoC file before this returns. */
tfw_status_t tfw_hal_init(void)
{
    return TFW_OK;
}

/* tfw-boot's handoff carries the cause IT read from the marker, before its
 * own reset handler cleared it; this image's reset handler ran after that and
 * found the marker clear. With no handoff, the own latch is the answer, as on
 * AVR (src/hal/avr/hal_sys.c). */
tfw_reset_cause_t tfw_hal_reset_cause(void)
{
    uint8_t handed_off;

    if (tfw_boot_handoff_reset_cause(&handed_off)) {
        return (tfw_reset_cause_t)handed_off;
    }
    return tfw_hal_early_reset_cause();
}

/* A system reset, not a jump to the reset vector: every peripheral returns
 * to its reset state and tfw-boot runs first, as after power-on. */
void tfw_hal_reset(void)
{
    tfw_cm_system_reset();
}

void tfw_hal_irq_enable(void)
{
    tfw_cm_irq_on();
}

/* Free SRAM: the gap between the end of .noinit (__heap_start; there is no
 * allocator) and the current stack, as on AVR. */
uint32_t tfw_hal_free_ram(void)
{
    uint32_t marker;

    return (uint32_t)((uintptr_t)&marker - (uintptr_t)&__heap_start);
}

void tfw_hal_watchdog_arm(void)
{
    tfw_cm_soc_watchdog_arm();
}

void tfw_hal_watchdog_pet(void)
{
    tfw_cm_soc_watchdog_pet();
}
