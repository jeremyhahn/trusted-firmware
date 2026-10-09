/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "platform_id.h"
#include "boot_platform.h"
#include "tfw/hal.h"

static uint8_t serial[TFW_BOOT_SERIAL_LEN];

void tfw_platform_id_init(void)
{
    uint8_t            raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t plat;

    memset(serial, 0, sizeof serial);

    if (tfw_hal_nvm_read(TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN) != TFW_OK) {
        return;
    }
    /* TFW_ERR_NOT_PROVISIONED (blank region), TFW_ERR_UNSUPPORTED (a layout
     * version this build does not know) and TFW_ERR_CRC (corrupted) all leave
     * the serial zero -- the region's contents are not to be trusted for
     * identity purposes in any of those cases, only its absence differs. */
    if (tfw_boot_platform_parse(raw, &plat) != TFW_OK) {
        return;
    }
    memcpy(serial, plat.serial, sizeof serial);
}

const uint8_t *tfw_platform_id_serial(void)
{
    return serial;
}

/* Not cached -- see platform_id.h. Re-reads and re-parses the whole block on
 * every call, exactly the same way tfw_platform_id_init() does once at boot. */
void tfw_platform_id_operator_pubkey(uint8_t out[TFW_BOOT_OPERATOR_PUBKEY_LEN])
{
    uint8_t            raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t plat;

    memset(out, 0, TFW_BOOT_OPERATOR_PUBKEY_LEN);
    if (tfw_hal_nvm_read(TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN) != TFW_OK) {
        return;
    }
    if (tfw_boot_platform_parse(raw, &plat) != TFW_OK) {
        return;
    }
    memcpy(out, plat.operator_pubkey, TFW_BOOT_OPERATOR_PUBKEY_LEN);
}
