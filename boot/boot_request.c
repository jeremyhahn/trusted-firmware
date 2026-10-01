/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "boot_request.h"
#include "core/reset_cause.h"

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

void ag_boot_request_encode(uint8_t *region)
{
    wr_u32(&region[AG_BOOT_REQ_OFF_MAGIC],   AG_BOOT_REQUEST_MAGIC);
    wr_u32(&region[AG_BOOT_REQ_OFF_INVERSE], AG_BOOT_REQUEST_INVERSE);
}

uint8_t ag_boot_request_valid(const uint8_t *region, uint8_t mcusr)
{
    const uint8_t causes = AG_MCUSR_PORF | AG_MCUSR_EXTRF | AG_MCUSR_BORF
                         | AG_MCUSR_WDRF;

    if ((mcusr & causes) != AG_MCUSR_WDRF) {
        return 0u;
    }
    if (rd_u32(&region[AG_BOOT_REQ_OFF_MAGIC]) != AG_BOOT_REQUEST_MAGIC) {
        return 0u;
    }
    return (uint8_t)(rd_u32(&region[AG_BOOT_REQ_OFF_INVERSE])
                     == AG_BOOT_REQUEST_INVERSE);
}
