/*
 * OTA_INFO's floor on the ATECC608A tier: the element's monotonic counter, read
 * as tfw_boot_floor_read (boot/tier/cdi_atecc.c) reads it. The platform block's
 * copy is not consulted: it is stale once an update has advanced the counter.
 * Any failure, a missing element included, is TFW_ERR_CRYPTO -- an element
 * build that cannot read its authority has no floor to report. See ota_floor.h.
 */

#include "ota_floor.h"
#include "../crypto/atecc608a/atecc.h"

tfw_status_t tfw_otactl_floor(const tfw_boot_platform_t *p, uint32_t *out)
{
    uint8_t      v[4];
    tfw_status_t st;

    (void)p;
    if (tfw_atecc_wake() != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }
    st = tfw_atecc_exec(TFW_ATECC_OP_COUNTER, TFW_ATECC_COUNTER_READ,
                       TFW_ATECC_COUNTER_BOOT, 0, 0u, v, 4u, TFW_ATECC_T_COUNTER);
    (void)tfw_atecc_sleep();
    if (st != TFW_OK) {
        return TFW_ERR_CRYPTO;
    }
    *out = (uint32_t)v[0] | ((uint32_t)v[1] << 8)
         | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    return TFW_OK;
}
