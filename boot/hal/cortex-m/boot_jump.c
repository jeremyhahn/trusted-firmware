/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's jump to the application on Cortex-M (#161), boot/boot_hal.h's
 * tfw_boot_jump_application. It is the one function of the core's boot HAL
 * with an Arm instruction in it, so it sits apart from boot_hal_cortex-m.c,
 * which the host compiles and tests (#628) and this file it cannot.
 */

#include <stdint.h>

#include "tfw/boot.h"
#include "tfw/config.h"
#include "../../boot_hal.h"
#include "boot_soc.h"
#include "cm_regs.h"
#include "hal_mpu.h"

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
    /* tfw-boot's own table is off before it leaves; the application's
     * tfw_cm_reset arms the application's, with tfw-boot hidden. */
    tfw_cm_mpu_off();
    tfw_cm_dsb();
    tfw_cm_isb();
    __asm__ volatile ("msr msp, %0\n\t"
                      "bx  %1"
                      : : "r"(sp), "r"(pc) : "memory");
    for (;;) { }   /* unreachable */
}
