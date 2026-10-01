/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "update_request.h"
#include "ag/hal.h"
#include "boot_request.h"

uint8_t ag_update_request_supported(void)
{
    return 1u;
}

void ag_update_request_arm(void)
{
    ag_boot_request_encode(ag_hal_handoff_block());
}
