/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The AVR's EEPROM on the STM32H5 (SPEC row stm32h5), over flash. The
 * address map is the AVR's and the generic Cortex-M file's below
 * TFW_BOOT_EE_LEN; above it the bytes are the application's own sector.
 *
 *   [0, TFW_BOOT_EE_LEN)                 the platform window, tfw-boot's span:
 *                                        read from TFW_CM_PLATFORM_BASE; a
 *                                        write is TFW_ERR_RANGE unless it is
 *                                        the whole confirm request, which keeps
 *                                        the generic file's store
 *   [TFW_BOOT_EE_LEN, TFW_EEPROM_SIZE)   the user sector, TFW_CM_USER_EE_BASE
 *                                        (board_profile.h), the 8 KiB sector
 *                                        directly below the platform block's
 *
 * A write to the user sector goes through tfw-boot's flash helper,
 * tfw_cm_boot_flash_write (boot/hal/cortex-m/stm32h5/boot_flash.c, over
 * h5_flash.c), linked into the application with this file and not copied.
 * The helper programs the quad-words the data touches where they are erased,
 * and otherwise copies the sector to RAM, merges the data, erases the sector
 * and programs it back, then reads the span back. This file reads the span
 * back once more. Data that does not read back is TFW_ERR_INTERNAL, as the
 * helper reports it (the status table has no I/O code); a locked or
 * write-protected interface is the helper's TFW_ERR_PROTECTED_REGION. The span never leaves the user
 * sector, so no write erases the platform sector. A reset between the erase and the last program loses the
 * user sector's contents, as a reset mid-write loses an AVR EEPROM cell; the
 * platform sector is not touched (docs/boards/stm32h573i-dk.md).
 */

#include <stdint.h>
#include <string.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "../../../../boot/hal/cortex-m/boot_soc.h"

_Static_assert(TFW_BOOT_CONFIRM_ADDR >= TFW_BOOT_EE_BASE + TFW_BOOT_EE_BLOCK_LEN
               && TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN <= TFW_BOOT_EE_LEN,
               "the confirm request is not between the platform block and the span's end");
_Static_assert(TFW_EEPROM_SIZE == TFW_BOOT_EE_LEN + TFW_CM_USER_EE_SIZE,
               "TFW_EEPROM_SIZE is not tfw-boot's span plus the user sector");
_Static_assert(TFW_BOOT_EE_LEN <= TFW_CM_PLATFORM_SIZE,
               "tfw-boot's span runs past the platform window");

#define USER_EE ((uint8_t *)(uintptr_t)TFW_CM_USER_EE_BASE)

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    uint32_t end = (uint32_t)addr + len;
    uint32_t low;
    tfw_status_t st;

    if (end > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    low = addr < TFW_BOOT_EE_LEN ? (end < TFW_BOOT_EE_LEN ? end : TFW_BOOT_EE_LEN) - addr : 0u;
    if (low != 0u) {
        st = tfw_hal_nvm_read(addr, buf, (uint8_t)low);
        if (st != TFW_OK) {
            return st;
        }
    }
    if (len > low) {
        memcpy(buf + low, USER_EE + (addr + low - TFW_BOOT_EE_LEN), len - low);
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint32_t off;
    tfw_status_t st;

    if ((uint32_t)addr + len > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    if ((uint32_t)addr < (uint32_t)TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN) {
        if (addr != TFW_BOOT_CONFIRM_ADDR || len != TFW_BOOT_CONFIRM_LEN) {
            return TFW_ERR_RANGE;
        }
        /* The generic file's path, unchanged by this file: how the confirm
         * request reaches flash is not this row's (the board page). */
        memcpy((uint8_t *)(uintptr_t)(TFW_CM_PLATFORM_BASE + addr), buf, len);
        return TFW_OK;
    }
    off = (uint32_t)addr - TFW_BOOT_EE_LEN;
    st = tfw_cm_boot_flash_write(TFW_CM_USER_EE_BASE + off, buf, len);
    if (st == TFW_OK && memcmp(USER_EE + off, buf, len) != 0) {
        st = TFW_ERR_INTERNAL;
    }
    return st;
}
