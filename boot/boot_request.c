/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "boot_platform.h"

#include "boot_request.h"

void tfw_boot_request_encode(uint8_t *region)
{
    tfw_boot_wr_u32(&region[TFW_BOOT_REQ_OFF_MAGIC],   TFW_BOOT_REQUEST_MAGIC);
    tfw_boot_wr_u32(&region[TFW_BOOT_REQ_OFF_INVERSE], TFW_BOOT_REQUEST_INVERSE);
}

uint8_t tfw_boot_request_valid(const uint8_t *region, tfw_reset_cause_t cause)
{
    if (cause != TFW_RESET_WATCHDOG) {
        return 0u;
    }
    if (tfw_boot_rd_u32(&region[TFW_BOOT_REQ_OFF_MAGIC]) != TFW_BOOT_REQUEST_MAGIC) {
        return 0u;
    }
    return (uint8_t)(tfw_boot_rd_u32(&region[TFW_BOOT_REQ_OFF_INVERSE])
                     == TFW_BOOT_REQUEST_INVERSE);
}
