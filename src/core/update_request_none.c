/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The no-bootloader stub, linked instead of update_request.c on a board with no
 * boot section -- see src/core/handoff_none.c for the same gate. RESET(1) is
 * answered TFW_ERR_UNSUPPORTED rather than quietly behaving as a soft reset: a
 * host that asked for the bootloader and got an application is told it did not
 * get what it asked for, while it still has a live connection to be told on.
 */
#include "update_request.h"

uint8_t tfw_update_request_supported(void)
{
    return 0u;
}

uint8_t tfw_update_request_session(void)
{
    return 0u;
}

void tfw_update_request_arm(void) {}
