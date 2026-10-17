/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OTA_BEGIN's backup of the running image (issue #52; docs/ota/architecture.md,
 * "ATmega328P and ATmega32U4: store-staged, one internal slot"). The store's
 * PREV is laid out as the application region itself, trailer at its end, which
 * is what tfw-boot copies back (boot/boot_prev.c) when the trial ends
 * unconfirmed or the new image is refused.
 *
 * Linked by mk/firmware.mk with the stager on a board without BOOT_AB; the
 * 2560 falls back to its other internal slot and links ota_prev_none.c.
 */
#include "tfw/boot.h"
#include "tfw/hal.h"
#include "ota_stage.h"
#include "ota_store.h"

#define PREV  ((uint32_t)TFW_OTA_STORE_PREV_ADDR)
#define CHUNK 64u   /* divides the NOR's 256-byte program page: no write crosses one */

tfw_status_t tfw_ota_prev_backup(void)
{
    uint8_t      buf[CHUNK];
    uint32_t     a;
    tfw_status_t st = TFW_OK;

    for (a = 0u; st == TFW_OK && a < TFW_BOOT_APP_SIZE; a += TFW_STORE_SECTOR) {
        st = tfw_store_erase(PREV + a);
    }
    for (a = 0u; st == TFW_OK && a < TFW_BOOT_APP_SIZE; a += CHUNK) {
        tfw_hal_flash_read(TFW_BOOT_APP_BASE + a, buf, CHUNK);
        st = tfw_store_write(PREV + a, buf, CHUNK);
    }
    return st;
}
