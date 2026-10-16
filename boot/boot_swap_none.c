/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * No second slot (every board but the 2560, and the 2560 rows with no update
 * path): the three calls main() makes change nothing, and LTO folds them.
 */
#include "boot_swap.h"

tfw_boot_reset_action_t tfw_boot_swap_at_reset(tfw_boot_platform_t *p,
                                               tfw_boot_reset_action_t act)
{
    (void)p;
    return act;
}

tfw_status_t tfw_boot_swap_take(tfw_boot_platform_t *p, uint32_t floor)
{
    (void)p;
    (void)floor;
    return TFW_OK;
}

tfw_status_t tfw_boot_swap_commit(tfw_boot_platform_t *p, uint32_t floor)
{
    (void)p;
    (void)floor;
    return TFW_OK;
}

tfw_status_t tfw_boot_swap_fall_back(tfw_boot_platform_t *p)
{
    (void)p;
    return TFW_ERR_BAD_STATE;
}
