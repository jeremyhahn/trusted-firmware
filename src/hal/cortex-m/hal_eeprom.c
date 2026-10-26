/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The AVR's EEPROM, on a part that has none: the platform window
 * (TFW_CM_PLATFORM_BASE, board_profile.h), addressed from its own zero as
 * tfw_boot_nvm_read addresses it, so an offset the application writes is the
 * offset tfw-boot reads. OTA_CONFIRM's request (TFW_BOOT_CONFIRM_ADDR,
 * src/core/ota_ctl.c) is the one writer on an A/B board.
 *
 * The bootloader's span [TFW_BOOT_EE_BASE, TFW_BOOT_EE_LEN) is tfw-boot's
 * alone, but for that one record: a write into it is TFW_ERR_RANGE unless it
 * is the whole confirm request, TFW_BOOT_CONFIRM_LEN bytes at
 * TFW_BOOT_CONFIRM_ADDR, which #231 placed inside the span when it grew the
 * span's end to 0x140. A span past the window's end is TFW_ERR_RANGE too.
 * Never a partial write. The window is RAM under QEMU, so a write is a store.
 */

#include <stdint.h>
#include <string.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"

_Static_assert(TFW_BOOT_CONFIRM_ADDR >= TFW_BOOT_EE_BASE + TFW_BOOT_EE_BLOCK_LEN
               && TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN <= TFW_BOOT_EE_LEN,
               "the confirm request is not between the platform block and the span's end");

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    return tfw_hal_nvm_read(addr, buf, len);
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > (uint32_t)TFW_CM_PLATFORM_SIZE) {
        return TFW_ERR_RANGE;
    }
    if ((uint32_t)addr < (uint32_t)TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN
        && (addr != TFW_BOOT_CONFIRM_ADDR || len != TFW_BOOT_CONFIRM_LEN)) {
        return TFW_ERR_RANGE;
    }
    memcpy((uint8_t *)(uintptr_t)(TFW_CM_PLATFORM_BASE + addr), buf, len);
    return TFW_OK;
}
