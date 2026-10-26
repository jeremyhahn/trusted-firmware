/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The read-modify-write tfw-boot's flash writes go through on the RP2350
 * (rp_flash.h). The flash erases a 4 KiB sector at a time and the platform
 * block is 192 bytes of one, so a write reads the sector, lays the bytes over
 * it and writes it back whole. A sector that already holds the bytes is left
 * alone: no erase is spent on a write that changes nothing.
 *
 * No register and no ROM call here, so the host test links this file as it
 * is and supplies the two sector functions over a RAM flash.
 */

#include <stdint.h>
#include <string.h>

#include "core/secure_zero.h"
#include "rp_flash.h"

/* One sector, in SRAM: the ROM programs from it while XIP is off, when no
 * byte in flash can be read. */
static uint8_t sector[TFW_RP_FLASH_SECTOR];

tfw_status_t tfw_rp_flash_update(uint32_t addr, const uint8_t *data, uint32_t len,
                                 uint32_t flash_end)
{
    tfw_status_t st = TFW_OK;

    if (addr < TFW_RP_XIP_BASE || addr > flash_end || len > flash_end - addr) {
        return TFW_ERR_RANGE;
    }
    while (len != 0u && st == TFW_OK) {
        uint32_t off  = addr - TFW_RP_XIP_BASE;
        uint32_t base = off & ~(uint32_t)(TFW_RP_FLASH_SECTOR - 1u);
        uint32_t at   = off - base;
        uint32_t n    = TFW_RP_FLASH_SECTOR - at;

        if (n > len) {
            n = len;
        }
        tfw_rp_flash_sector_read(base, sector);
        if (memcmp(&sector[at], data, n) != 0) {
            memcpy(&sector[at], data, n);
            st = tfw_rp_flash_sector_write(base, sector);
        }
        addr += n;
        data += n;
        len  -= n;
    }
    /* The block's bytes do not outlive the write in SRAM the application
     * takes over. */
    tfw_secure_zero(sector, sizeof sector);
    return st;
}
