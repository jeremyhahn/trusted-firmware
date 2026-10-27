/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's hardware layer on Cortex-M (#161): the core's half of
 * boot/boot_hal.h. The SoC's half -- the UART -- is boot/hal/cortex-m/<soc>/,
 * and the watchdog both images share is src/hal/cortex-m/<soc>/hal_soc.c,
 * which mk/boot.mk links here as well.
 *
 * On the first board (mps2-an505 under QEMU) the code regions are SRAM that
 * QEMU loads the images into, so a flash page is written by a copy. A part
 * with a flash controller (stm32h5, rp2350 through its boot ROM, nrf54l's
 * RRAM) has its program sequence in its SoC directory. Either way this file
 * calls tfw_cm_boot_flash_write (boot_soc.h); the range checks stay here.
 */

#include <stdint.h>
#include <string.h>

#include "tfw/boot.h"
#include "tfw/config.h"
#include "../../boot_hal.h"
#include "boot_slots.h"     /* TFW_APP_WRITE_END on the A/B rows */
#include "boot_soc.h"
#include "cm_regs.h"
#include "hal_early.h"   /* the latch, the reset and the watchdog: shared with the application */

/* --------------------------------------------------------------- flash */

void tfw_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    memcpy(buf, (const void *)(uintptr_t)addr, len);
}

uint16_t tfw_boot_flash_page_size(void)
{
    return (uint16_t)TFW_OTA_PAGE_MAX;
}

/* The application region and nothing else, as on AVR and for its reason: an
 * OTA image one page too long must not overwrite tfw-boot, and here tfw-boot's
 * region is writable memory, so this check is the only thing that stops it. */
tfw_status_t tfw_boot_flash_write_page(uint32_t addr, const uint8_t *data)
{
    if ((addr % (uint32_t)TFW_OTA_PAGE_MAX) != 0u) {
        return TFW_ERR_RANGE;
    }
    if (addr < (uint32_t)TFW_APP_REGION_BASE ||
        addr + (uint32_t)TFW_OTA_PAGE_MAX > (uint32_t)TFW_APP_WRITE_END) {
        return TFW_ERR_PROTECTED_REGION;
    }
    return tfw_cm_boot_flash_write(addr, data, (uint32_t)TFW_OTA_PAGE_MAX);
}

/* ----------------------------------------------------------------- nvm */

/* The platform block: TFW_CM_PLATFORM_SIZE bytes at TFW_CM_PLATFORM_BASE
 * (board_profile.h), which the application reads through
 * src/hal/cortex-m/hal_nvm.c. */
_Static_assert((uint32_t)TFW_BOOT_EE_BASE + TFW_BOOT_EE_BLOCK_LEN <= TFW_CM_PLATFORM_SIZE,
               "the platform block must fit in TFW_CM_PLATFORM_SIZE");

tfw_status_t tfw_boot_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > (uint32_t)TFW_CM_PLATFORM_SIZE) {
        return TFW_ERR_RANGE;
    }
    memcpy(buf, (const void *)(uintptr_t)(TFW_CM_PLATFORM_BASE + addr), len);
    return TFW_OK;
}

tfw_status_t tfw_boot_nvm_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > (uint32_t)TFW_CM_PLATFORM_SIZE) {
        return TFW_ERR_RANGE;
    }
    return tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE + addr, buf, len);
}

/* ------------------------------------------------------------- handoff */

/* The application's src/hal/cortex-m/hal_handoff.c names the same section,
 * which the linker script pins to the same address in both images. */
static uint8_t handoff[TFW_BOOT_HANDOFF_LEN] __attribute__((section(".handoff")));

void tfw_boot_handoff_write(const uint8_t *block, uint8_t len)
{
    memcpy(handoff, block, len);
}

void tfw_boot_handoff_read(uint8_t *buf, uint8_t len)
{
    memcpy(buf, handoff, len);
}

/* ------------------------------------------------------------- control */

/* The first boot-HAL call main() makes, before the platform block is read:
 * on a two-slot board the factory copies go in here while their homes are
 * blank (boot_factory.c); elsewhere the call is empty and LTO drops it. */
tfw_reset_cause_t tfw_boot_reset_cause(void)
{
    tfw_cm_boot_factory();
    return tfw_hal_early_reset_cause();
}

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

/* A system reset request, which marks itself as the application's
 * tfw_hal_reset does (src/hal/cortex-m/hal_early.c). */
void tfw_boot_reset(void)
{
    tfw_cm_system_reset();
}

/* The SoC's watchdog, at the period src/hal/cortex-m/<soc>/hal_soc.c states:
 * on mps2 the reset lands 8 s after the last pet, AVR's longest period. */
void tfw_boot_watchdog_arm(void)
{
    tfw_cm_soc_watchdog_arm();
}
