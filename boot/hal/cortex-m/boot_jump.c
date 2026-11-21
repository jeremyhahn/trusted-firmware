/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's jump to the application on Cortex-M (boot/boot_hal.h), apart from
 * the rest of the core's boot HAL (boot_hal_cortex-m.c) because it is the one
 * piece that needs the core itself: the barriers and the MSP write. The rest
 * builds and runs on the host; this file builds for the target only, and the
 * QEMU boot tests are what run it.
 */

#include <stdint.h>

#include "tfw/boot.h"
#include "tfw/config.h"
#include "../../boot_hal.h"
#include "boot_soc.h"
#include "cm_regs.h"

/* The application's vector table is the first thing in its region (the
 * linker script's .vectors): its stack top, then its reset handler. tfw-boot
 * enabled no interrupt and started no timer, so the UART is the only
 * peripheral to put back. The application's reset handler points VTOR at its
 * own table, as it does after a bare reset, so nothing here touches VTOR. */
void tfw_boot_jump_application(void)
{
    const volatile uint32_t *vt = (const volatile uint32_t *)(uintptr_t)TFW_BOOT_APP_BASE;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];

    tfw_cm_boot_uart_off();
    tfw_cm_dsb();
    tfw_cm_isb();
    __asm__ volatile ("msr msp, %0\n\t"
                      "bx  %1"
                      : : "r"(sp), "r"(pc) : "memory");
    for (;;) { }   /* unreachable */
}
