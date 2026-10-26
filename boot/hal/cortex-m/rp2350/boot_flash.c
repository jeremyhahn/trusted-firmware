/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's flash writes on the RP2350: boot_soc.h's entry, over the
 * board's 4 MiB of flash (TFW_FLASH_TOTAL, board_profile.h). rp_flash.h has
 * the rest.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "../boot_soc.h"
#include "rp_flash.h"

tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    return tfw_rp_flash_update(addr, data, len, (uint32_t)TFW_FLASH_TOTAL);
}
