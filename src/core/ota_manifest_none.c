/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stub linked in place of ota_manifest.c on a board that does not link the
 * SUIT reader (no OTA_MANIFEST := 1, or no element tier): OTA_ACTIVATE holds
 * the image to tfw-boot's signature and the trailer alone.
 */
#include "ota_stage.h"

tfw_status_t tfw_ota_manifest(tfw_boot_platform_t *p)
{
    (void)p;
    return TFW_OK;
}
