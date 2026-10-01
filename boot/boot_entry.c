/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ag-boot's half of the update-session request: the part that touches the boot
 * HAL. Split from boot_request.c because the application links that file for
 * the encoder and has no boot HAL to link this against.
 */

#include "boot_hal.h"
#include "boot_request.h"

uint8_t ag_boot_request_take(uint8_t mcusr)
{
    uint8_t req[AG_BOOT_REQUEST_LEN];
    static const uint8_t zero[AG_BOOT_REQUEST_LEN] = { 0u };
    uint8_t valid;

    ag_boot_handoff_read(req, AG_BOOT_REQUEST_LEN);
    valid = ag_boot_request_valid(req, mcusr);
    ag_boot_handoff_write(zero, AG_BOOT_REQUEST_LEN);
    return valid;
}
