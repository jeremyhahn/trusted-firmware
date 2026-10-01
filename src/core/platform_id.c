/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "platform_id.h"
#include "boot_platform.h"
#include "ag/hal.h"

static uint8_t serial[AG_BOOT_SERIAL_LEN];

void ag_platform_id_init(void)
{
    uint8_t            raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t plat;

    memset(serial, 0, sizeof serial);

    if (ag_hal_eeprom_read(AG_BOOT_EE_BASE, raw, AG_BOOT_EE_LEN) != AG_OK) {
        return;
    }
    /* AG_ERR_NOT_PROVISIONED (blank region), AG_ERR_UNSUPPORTED (a layout
     * version this build does not know) and AG_ERR_CRC (corrupted) all leave
     * the serial zero -- the region's contents are not to be trusted for
     * identity purposes in any of those cases, only its absence differs. */
    if (ag_boot_platform_parse(raw, &plat) != AG_OK) {
        return;
    }
    memcpy(serial, plat.serial, sizeof serial);
}

const uint8_t *ag_platform_id_serial(void)
{
    return serial;
}
