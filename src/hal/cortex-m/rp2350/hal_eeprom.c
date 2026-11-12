/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The AVR's EEPROM on the RP2350 (row rp2350), where the platform window is
 * QSPI flash behind XIP and a store to it writes nothing. The AVR's map is
 * kept: an address below TFW_BOOT_EE_LEN is tfw-boot's span, read from the
 * platform window (TFW_CM_PLATFORM_BASE) as the generic file reads it, and a
 * write into it is refused as the generic file refuses it, the confirm
 * request (TFW_BOOT_CONFIRM_ADDR, TFW_BOOT_CONFIRM_LEN) excepted, which keeps
 * the generic file's store. From TFW_BOOT_EE_LEN up to TFW_EEPROM_SIZE an
 * address is a byte of the application's own sector, TFW_CM_USER_EE_BASE
 * (board_profile.h), the sector directly below the platform block's.
 *
 * A write is a read-modify-write of that one sector, through tfw-boot's flash
 * helper (boot/hal/cortex-m/rp2350/rp_flash.h): tfw_rp_flash_update reads the
 * sector into its RAM buffer, merges the bytes, and erases and programs it
 * whole through the boot ROM with XIP off (rp_flash_rom.c), then the bytes are
 * read back through XIP and compared; a mismatch is TFW_ERR_INTERNAL, the
 * status the ESP32's flash EEPROM gives a failed flash write (status.h has
 * no I/O code). The span is
 * checked before anything is touched, so a refused write writes nothing, and
 * no address this file computes reaches the platform sector: the two
 * _Static_asserts below hold the user span inside the user sector. A power
 * loss between the erase and the program loses the user sector only, as a
 * power loss mid-write loses the AVR's EEPROM bytes.
 */
#include <stdint.h>
#include <string.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "rp_flash.h"

#define USER_EE_LEN ((uint32_t)TFW_EEPROM_SIZE - (uint32_t)TFW_BOOT_EE_LEN)

_Static_assert(TFW_BOOT_EE_BASE == 0u, "the span starts at the EEPROM's zero");
_Static_assert((uint32_t)TFW_EEPROM_SIZE > (uint32_t)TFW_BOOT_EE_LEN,
               "the EEPROM has bytes past tfw-boot's span");
_Static_assert(USER_EE_LEN <= (uint32_t)TFW_CM_USER_EE_SIZE
               && (uint32_t)TFW_CM_USER_EE_SIZE == TFW_RP_FLASH_SECTOR,
               "the user span fits the one user sector");
_Static_assert((uint32_t)TFW_CM_USER_EE_BASE % TFW_RP_FLASH_SECTOR == 0u
               && (uint32_t)TFW_CM_USER_EE_BASE + TFW_CM_USER_EE_SIZE
                  <= (uint32_t)TFW_CM_PLATFORM_BASE,
               "the user sector is a whole sector below the platform block");

#define USER_EE ((const uint8_t *)(uintptr_t)TFW_CM_USER_EE_BASE)

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    uint32_t end = (uint32_t)addr + len;
    uint32_t low = 0u;

    if (end > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    if (addr < TFW_BOOT_EE_LEN) {
        tfw_status_t st;

        low = (end < TFW_BOOT_EE_LEN ? end : TFW_BOOT_EE_LEN) - addr;
        st  = tfw_hal_nvm_read(addr, buf, (uint8_t)low);
        if (st != TFW_OK) {
            return st;
        }
    }
    if (end > (uint32_t)addr + low) {
        memcpy(&buf[low], &USER_EE[(uint32_t)addr + low - TFW_BOOT_EE_LEN], len - low);
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint32_t at;
    tfw_status_t st;

    if ((uint32_t)addr + len > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    if (addr < TFW_BOOT_EE_LEN) {
        if (addr != TFW_BOOT_CONFIRM_ADDR || len != TFW_BOOT_CONFIRM_LEN) {
            return TFW_ERR_RANGE;
        }
        /* The generic file's store, unchanged: how OTA_CONFIRM's request
         * reaches flash on this part is not this file's (the board page). */
        memcpy((uint8_t *)(uintptr_t)(TFW_CM_PLATFORM_BASE + addr), buf, len);
        return TFW_OK;
    }
    if (len == 0u) {
        return TFW_OK;
    }
    at = (uint32_t)addr - TFW_BOOT_EE_LEN;
    st = tfw_rp_flash_update((uint32_t)TFW_CM_USER_EE_BASE + at, buf, len,
                             (uint32_t)TFW_FLASH_TOTAL);
    if (st != TFW_OK) {
        return st;
    }
    if (memcmp(&USER_EE[at], buf, len) != 0) {
        return TFW_ERR_INTERNAL;
    }
    return TFW_OK;
}
