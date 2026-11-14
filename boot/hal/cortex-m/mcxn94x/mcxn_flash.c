/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The read-modify-write tfw-boot's flash writes go through on the MCXN947
 * (mcxn_flash.h), over the boot ROM's flash driver as the caller hands it in.
 * No register and no ROM address here, so the host test links this file as it
 * is.
 */

#include <stdint.h>
#include <string.h>

#include "core/secure_zero.h"
#include "mcxn_flash.h"

#define ROM_SUCCESS 0

/* One sector, in SRAM: flash_program takes its source from here. */
static uint8_t sector[TFW_MCXN_FLASH_SECTOR];

tfw_status_t tfw_mcxn_flash_sector(const tfw_mcxn_flash_api_t *api,
                                   tfw_mcxn_flash_config_t *config,
                                   uint32_t off, uint8_t *src)
{
    uint32_t start;

    if ((off & (TFW_MCXN_FLASH_SECTOR - 1u)) != 0u) {
        return TFW_ERR_RANGE;
    }
    /* The driver's own addresses: the flash from its block's base, as the
     * SDK's examples take it from kFLASH_PropertyPflashBlockBaseAddr. */
    start = config->pflash_block_base + off;
    if (api->flash_erase(config, start, TFW_MCXN_FLASH_SECTOR,
                         TFW_MCXN_ROM_ERASE_KEY) != ROM_SUCCESS ||
        api->flash_program(config, start, src, TFW_MCXN_FLASH_SECTOR) != ROM_SUCCESS) {
        return TFW_ERR_INTERNAL;
    }
    return TFW_OK;
}

tfw_status_t tfw_mcxn_flash_update(const tfw_mcxn_flash_api_t *api, const uint8_t *flash,
                                   uint32_t flash_end, uint32_t addr,
                                   const uint8_t *data, uint32_t len)
{
    tfw_mcxn_flash_config_t config;
    tfw_status_t st = TFW_OK;

    if (addr < TFW_MCXN_FLASH_BASE || addr > flash_end || len > flash_end - addr) {
        return TFW_ERR_RANGE;
    }
    if (len == 0u) {
        return TFW_OK;
    }
    memset(&config, 0, sizeof config);
    if (api->flash_init(&config) != ROM_SUCCESS ||
        config.pflash_sector_size != TFW_MCXN_FLASH_SECTOR) {
        return TFW_ERR_INTERNAL;
    }
    while (len != 0u && st == TFW_OK) {
        uint32_t off  = addr - TFW_MCXN_FLASH_BASE;
        uint32_t base = off & ~(uint32_t)(TFW_MCXN_FLASH_SECTOR - 1u);
        uint32_t at   = off - base;
        uint32_t n    = TFW_MCXN_FLASH_SECTOR - at;

        if (n > len) {
            n = len;
        }
        memcpy(sector, &flash[base], sizeof sector);
        if (memcmp(&sector[at], data, n) != 0) {
            memcpy(&sector[at], data, n);
            st = tfw_mcxn_flash_sector(api, &config, base, sector);
            /* The whole sector reads back as it was with data merged in,
             * not only data's span. */
            if (st == TFW_OK && memcmp(&flash[base], sector, sizeof sector) != 0) {
                st = TFW_ERR_INTERNAL;
            }
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
