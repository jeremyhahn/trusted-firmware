/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The update request on an image whose update path is the external store,
 * linked instead of update_request.c by mk/firmware.mk where the board is the
 * A/B part (BOOT_AB, the 2560) and the image stages (OTA_STAGE=1). Each image
 * takes one update path (#52, docs/ota/architecture.md, "The 2560 with OTA and
 * the store"): this one stages with OTA_BEGIN/OTA_DATA/OTA_END and installs with
 * OTA_ACTIVATE, and its tfw-boot (BOOT_STORE=1) opens no session over the UART.
 * RESET(1) is answered TFW_ERR_UNSUPPORTED rather than reset into a boot that
 * would only boot the image again. The board still has a tfw-boot that serves an
 * update path, so an application's OTA slots still count.
 */
#include "update_request.h"

uint8_t tfw_update_request_supported(void)
{
    return 1u;
}

uint8_t tfw_update_request_session(void)
{
    return 0u;
}

void tfw_update_request_arm(void) {}
