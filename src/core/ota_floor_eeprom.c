/*
 * OTA_INFO's floor on a tier with no element: the platform block's copy, which
 * is the one tfw-boot enforces there. See ota_floor.h.
 */

#include "ota_floor.h"

tfw_status_t tfw_otactl_floor(const tfw_boot_platform_t *p, uint32_t *out)
{
    *out = p->rollback_floor;
    return TFW_OK;
}
