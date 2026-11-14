/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The AVR's EEPROM on the nRF54L15: the platform window
 * (TFW_CM_PLATFORM_BASE, board_profile.h), which is RRAM here, addressed from
 * its own zero as tfw_boot_nvm_read addresses it. The rules are the generic
 * file's (src/hal/cortex-m/hal_eeprom.c): tfw-boot's span
 * [TFW_BOOT_EE_BASE, TFW_BOOT_EE_LEN) is TFW_ERR_RANGE but for the whole
 * confirm request, TFW_BOOT_CONFIRM_LEN bytes at TFW_BOOT_CONFIRM_ADDR, and a
 * span past the window's end is TFW_ERR_RANGE too. Both are checked before
 * the RRAM controller's write enable opens, so a refused write stores nothing.
 *
 * Where the generic file stores, this one goes through the RRAMC sequence
 * tfw-boot uses (rram_write.h): a plain store to RRAM programs nothing.
 * A read is a load, as on any memory-mapped part.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "rram_write.h"

_Static_assert(TFW_BOOT_CONFIRM_ADDR >= TFW_BOOT_EE_BASE + TFW_BOOT_EE_BLOCK_LEN
               && TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN <= TFW_BOOT_EE_LEN,
               "the confirm request is not between the platform block and the span's end");
_Static_assert((uint32_t)TFW_CM_PLATFORM_BASE + (uint32_t)TFW_CM_PLATFORM_SIZE <= TFW_NRF54L_RRAM_SIZE,
               "the platform window must lie in the RRAM");
_Static_assert((uint32_t)TFW_CM_PLATFORM_BASE >= (uint32_t)TFW_BOOT_REGION_BASE + (uint32_t)TFW_BOOT_REGION_SIZE,
               "the platform window must lie above tfw-boot's region");
_Static_assert(TFW_EEPROM_SIZE <= TFW_CM_PLATFORM_SIZE,
               "the EEPROM is the platform window or less");

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
    return tfw_nrf54l_rram_write((uint32_t)TFW_CM_PLATFORM_BASE + addr, buf, len);
}
