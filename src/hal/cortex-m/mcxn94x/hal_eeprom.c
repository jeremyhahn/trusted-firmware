/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The AVR's EEPROM on the MCXN947, whose flash erases a sector at a time.
 * Picked over src/hal/cortex-m/hal_eeprom.c by hal_src (mk/firmware.mk).
 *
 * The addresses keep the AVR's map:
 *
 *   [0, TFW_BOOT_EE_LEN)                the platform window
 *                                       (TFW_CM_PLATFORM_BASE), read as
 *                                       tfw_boot_nvm_read reads it; a write
 *                                       is refused as the generic file
 *                                       refuses it, but for OTA_CONFIRM's
 *                                       request, which keeps that file's
 *                                       store path
 *   [TFW_BOOT_EE_LEN, TFW_EEPROM_SIZE)  the user sector, TFW_CM_USER_EE_BASE
 *                                       from its own zero (board_profile.h),
 *                                       the sector below the platform block's
 *
 * A write to the user sector goes through tfw-boot's flash helper
 * (boot/hal/cortex-m/mcxn94x/mcxn_flash.c, linked here, not copied): it reads
 * the sector into its RAM buffer, lays the bytes over it, erases it and
 * programs it whole through the boot ROM's flash driver. The helper is handed
 * the user sector's end as the flash's end, so a span that would reach the
 * platform sector is TFW_ERR_RANGE before any ROM call: no write here erases
 * tfw-boot's sector. The bytes are then read back; a mismatch is
 * TFW_ERR_INTERNAL (include/tfw/status.h has no I/O status). A span past
 * TFW_EEPROM_SIZE is TFW_ERR_RANGE. Never a partial write of the span asked
 * for, short of a power loss mid-write, which loses the user sector only.
 *
 * The device's flash, its end and the ROM driver are the three things the
 * host test (test/unit/test_mcxn94x_eeprom.c) replaces with a model, by
 * defining TFW_MCXN_EE_FLASH, TFW_MCXN_EE_WINDOW, TFW_MCXN_EE_API before this file is compiled; its register model
 * (test/model/cortex-m/mcxn94x/cm_tick_model.h: SysTick, DWT and PRIMASK)
 * stands in for cm_regs.h, so the lock pair here is the one under test.
 */

#include <stdint.h>
#include <string.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "../../../../boot/hal/cortex-m/mcxn94x/mcxn_flash.h"

/* A host test includes its register model first; its guard keeps these out. */
#include "../cm_regs.h"
#include "../cm_cycles.h"

#ifndef TFW_MCXN_EE_FLASH
/* The flash as the core reads it, flash[0] at TFW_MCXN_FLASH_BASE. */
#define TFW_MCXN_EE_FLASH  ((const uint8_t *)(uintptr_t)TFW_MCXN_FLASH_BASE)
/* The platform window the confirm request is stored to. */
#define TFW_MCXN_EE_WINDOW ((uint8_t *)(uintptr_t)TFW_CM_PLATFORM_BASE)
/* The boot ROM's flash driver, the API tree's fifth word (mcxn_flash.h). */
#define TFW_MCXN_EE_API \
    ((const tfw_mcxn_flash_api_t *)(uintptr_t) \
         ((const volatile uint32_t *)(uintptr_t)TFW_MCXN_ROM_API_TREE)[TFW_MCXN_ROM_TREE_FLASH])

#endif

/* No interrupt runs from the flash while the ROM erases and programs it, as
 * tfw-boot's own write (boot/hal/cortex-m/mcxn94x/boot_flash.c). The sector
 * update masks them for longer than a millisecond, where SysTick keeps one
 * pending interrupt for all its reloads: the masked span is measured on
 * DWT's cycle counter and the ticks it would drop are added back
 * (tfw_cm_tick_catch_up) before interrupts return, so millis and every
 * deadline on it keep the time the write took. */
typedef struct {
    uint32_t primask;
    uint32_t cvr;
    uint32_t cycles;
} ee_mask_t;

static void ee_lock(ee_mask_t *m)
{
    TFW_CM_DEMCR |= TFW_CM_DEMCR_TRCENA;
    TFW_CM_DWT_CTRL |= TFW_CM_DWT_CTRL_CYCCNTENA;
    m->primask = tfw_cm_irq_save();
    m->cvr     = TFW_CM_SYST_CVR;
    m->cycles  = TFW_CM_DWT_CYCCNT;
}

static void ee_unlock(const ee_mask_t *m)
{
    /* The writes complete before anything reads the flash they wrote. */
    tfw_cm_dsb();
    tfw_cm_isb();
    tfw_cm_tick_catch_up(m->cvr, TFW_CM_DWT_CYCCNT - m->cycles);
    tfw_cm_irq_restore(m->primask);
}

/* The first address of the user span. */
#define EE_USER_LO  ((uint32_t)TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN)
#define EE_USER_END ((uint32_t)TFW_CM_USER_EE_BASE + TFW_CM_USER_EE_SIZE)

_Static_assert(TFW_BOOT_CONFIRM_ADDR >= TFW_BOOT_EE_BASE + TFW_BOOT_EE_BLOCK_LEN
               && TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN <= TFW_BOOT_EE_LEN,
               "the confirm request is not between the platform block and the span's end");
_Static_assert(TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN <= TFW_CM_PLATFORM_SIZE,
               "the bootloader's span does not fit the platform window");
_Static_assert(TFW_EEPROM_SIZE > TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN
               && TFW_EEPROM_SIZE - (TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN) <= TFW_CM_USER_EE_SIZE,
               "the user span does not fit the user sector");
_Static_assert(TFW_CM_USER_EE_SIZE == TFW_MCXN_FLASH_SECTOR
               && (TFW_CM_USER_EE_BASE - TFW_MCXN_FLASH_BASE) % TFW_MCXN_FLASH_SECTOR == 0u,
               "the user EEPROM is not one whole flash sector");

static const uint8_t *ee_at(uint32_t flash_addr)
{
    return &TFW_MCXN_EE_FLASH[flash_addr - TFW_MCXN_FLASH_BASE];
}

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    uint32_t a   = addr;
    uint32_t end = a + len;

    if (end > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    if (a < EE_USER_LO) {
        uint32_t n = (end < EE_USER_LO ? end : EE_USER_LO) - a;
        memcpy(buf, ee_at((uint32_t)TFW_CM_PLATFORM_BASE + a), n);
        buf += n;
        a   += n;
    }
    if (a < end) {
        memcpy(buf, ee_at((uint32_t)TFW_CM_USER_EE_BASE + (a - EE_USER_LO)), end - a);
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint32_t at;
    ee_mask_t mask;
    tfw_status_t st;

    if ((uint32_t)addr + len > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    if ((uint32_t)addr < EE_USER_LO) {
        if (addr != TFW_BOOT_CONFIRM_ADDR || len != TFW_BOOT_CONFIRM_LEN) {
            return TFW_ERR_RANGE;
        }
        /* The generic file's store path; how the request reaches flash is
         * not changed here. */
        memcpy(TFW_MCXN_EE_WINDOW + addr, buf, len);
        return TFW_OK;
    }
    if (len == 0u) {
        return TFW_OK;
    }
    at = (uint32_t)TFW_CM_USER_EE_BASE + ((uint32_t)addr - EE_USER_LO);
    /* The update erases and reprograms the whole sector and checks all of
     * it, the old bytes with buf merged in, against the flash it reads
     * back: a byte lost outside buf's span is an error too. */
    ee_lock(&mask);
    st = tfw_mcxn_flash_update(TFW_MCXN_EE_API, TFW_MCXN_EE_FLASH, EE_USER_END, at, buf, len);
    ee_unlock(&mask);
    return st;
}
