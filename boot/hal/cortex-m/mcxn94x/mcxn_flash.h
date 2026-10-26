/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's flash writes on the MCXN947, through the boot ROM's flash driver
 * as the SDK calls it. Every value is the mcux-sdk commit
 * docs/reference/README.md pins:
 *
 *   drivers/mcx_romapi/flash/src/fsl_flash.c
 *     19       the API tree at 0x1303FC00
 *     128-132  the tree: runBootloader, version, copyright, reserved0, then
 *              flashDriver, the fifth word
 *     51-57    the driver's interface: a 4-byte version (47-48), then
 *              flash_init, flash_erase and flash_program, one word each
 *     155, 178, 190  FLASH_Init, FLASH_Erase and FLASH_Program call them
 *              with their arguments as they are
 *   drivers/mcx_romapi/flash/fsl_flash.h
 *     79       kStatus_FLASH_Success is 0
 *     160      kFLASH_ApiEraseKey, FOUR_CHAR_CODE('l','f','e','k') (148)
 *     198      kFLASH_AlignementUnitProgram, 512 bytes
 *     253-308  flash_config_t and the two structs in it, which flash_init
 *              fills
 *   devices/MCXN947/MCXN947_cm33_core0_features.h
 *     947, 949 a 128-byte page and an 8 KiB sector
 *
 * The flash erases a sector at a time, so a write is a read-modify-write of
 * whole sectors, as on the RP2350: read the sector, lay the bytes over it,
 * erase it and program it back whole. A sector is 16 program units, so a whole
 * one meets every alignment the driver asks. A sector that already holds the
 * bytes is left alone.
 *
 * No register and no fixed address here: the caller hands in the driver's
 * interface, the flash as the core reads it, and the flash's end, so the host
 * test (test/test_mcxn_flash.c) supplies a model of all three.
 */
#ifndef TFW_BOOT_CM_MCXN94X_FLASH_H
#define TFW_BOOT_CM_MCXN94X_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "tfw/status.h"

#define TFW_MCXN_FLASH_BASE      0x10000000u   /* the Secure alias tfw-boot runs from */
#define TFW_MCXN_FLASH_SECTOR    8192u
#define TFW_MCXN_ROM_API_TREE    0x1303FC00u
#define TFW_MCXN_ROM_TREE_FLASH  4u            /* the tree's word that points at the driver */
#define TFW_MCXN_ROM_ERASE_KEY   0x6B65666Cu   /* 'l','f','e','k', the first in the low byte */

/* flash_config_t, field for field. flash_init fills it; the core reads the
 * block's base and the sector size back from it. */
typedef struct {
    uint32_t pflash_block_base;
    uint32_t pflash_total_size;
    uint32_t pflash_block_count;
    uint32_t pflash_page_size;
    uint32_t pflash_sector_size;
    uint32_t ffr_config[5];   /* flash_ffr_config_t */
    uint32_t mode_config[5];  /* flash_mode_config_t: a word, then three 4-byte structs */
    uint32_t *nboot_ctx;
    bool use_ahb_read;
} tfw_mcxn_flash_config_t;

/* The head of flash_driver_interface_t: as far as flash_program. The calls
 * return the SDK's status_t, an int32_t. */
typedef struct {
    uint32_t version;
    int32_t (*flash_init)(tfw_mcxn_flash_config_t *config);
    int32_t (*flash_erase)(tfw_mcxn_flash_config_t *config, uint32_t start,
                           uint32_t length, uint32_t key);
    int32_t (*flash_program)(tfw_mcxn_flash_config_t *config, uint32_t start,
                             uint8_t *src, uint32_t length);
} tfw_mcxn_flash_api_t;

/* Writes len bytes at addr, an address in the Secure alias, with flash the
 * part's bytes as the core reads them (flash[0] at TFW_MCXN_FLASH_BASE) and
 * flash_end the alias's end. TFW_ERR_RANGE for a span outside the flash,
 * before any ROM call; TFW_ERR_INTERNAL for a driver that fails a call, or
 * reports a sector of another size, the write stopping there. */
tfw_status_t tfw_mcxn_flash_update(const tfw_mcxn_flash_api_t *api, const uint8_t *flash,
                                   uint32_t flash_end, uint32_t addr,
                                   const uint8_t *data, uint32_t len);

/* One sector, erased and programmed from src. off is the sector's offset
 * from TFW_MCXN_FLASH_BASE; TFW_ERR_RANGE unless it is a sector boundary,
 * before any ROM call. */
tfw_status_t tfw_mcxn_flash_sector(const tfw_mcxn_flash_api_t *api,
                                   tfw_mcxn_flash_config_t *config,
                                   uint32_t off, uint8_t *src);

#endif /* TFW_BOOT_CM_MCXN94X_FLASH_H */
