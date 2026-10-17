/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot on a board with no external OTA store (mk/boot.mk, BOOT_STORE=0):
 * there is never a staged image to take. Linked instead of boot_stage.c.
 */
#include "boot_stage.h"

tfw_status_t tfw_boot_stage_take(tfw_boot_platform_t *p, uint32_t floor)
{
    (void)p;
    (void)floor;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_boot_stage_forget(void)
{
    return TFW_ERR_UNSUPPORTED;
}

void tfw_boot_stage_after_reset(tfw_boot_reset_action_t trial, tfw_boot_reset_action_t act)
{
    (void)trial;
    (void)act;
}
