/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "update_request.h"
#include "tfw/hal.h"
#include "boot_request.h"

uint8_t tfw_update_request_supported(void)
{
    return 1u;
}

uint8_t tfw_update_request_session(void)
{
    return 1u;
}

void tfw_update_request_arm(void)
{
    tfw_boot_request_encode(tfw_hal_handoff_block());
}
