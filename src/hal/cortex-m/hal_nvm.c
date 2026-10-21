/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's two read-only windows onto what tfw-boot owns: the
 * platform block (include/tfw/boot.h's TFW_BOOT_EE_*) and the code region.
 *
 * A Cortex-M part maps both into the address space, so each is a copy. The
 * platform block is TFW_CM_PLATFORM_SIZE bytes at TFW_CM_PLATFORM_BASE
 * (board_profile.h), addressed from its own zero as tfw_boot_nvm_read
 * addresses it (boot/boot_hal.h); a span past its end is TFW_ERR_RANGE,
 * never a partial copy. A flash address is the part's own: the application
 * slot is TFW_BOOT_APP_BASE onwards, as the callers already add.
 */

#include <stdint.h>
#include <string.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"

_Static_assert(TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN <= TFW_CM_PLATFORM_SIZE,
               "the platform block does not fit the board's NVM window");

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > (uint32_t)TFW_CM_PLATFORM_SIZE) {
        return TFW_ERR_RANGE;
    }
    memcpy(buf, (const uint8_t *)(uintptr_t)(TFW_CM_PLATFORM_BASE + addr), len);
    return TFW_OK;
}

void tfw_hal_flash_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    memcpy(buf, (const uint8_t *)(uintptr_t)addr, len);
}
