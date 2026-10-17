/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATmega328P's and ATmega32U4's fallback (issue #52; docs/ota/architecture.md,
 * "ATmega328P and ATmega32U4: store-staged, one internal slot"). One internal
 * slot, and the image before the round in the store's PREV, where the
 * application backed it up at OTA_BEGIN.
 *
 * The boot_swap.h seam main() already calls, linked in place of
 * boot_swap_none.c on a board without BOOT_AB whose row reads the store
 * (mk/boot.mk). An ended trial at reset, and a pending image the gate
 * refuses, copy PREV back and end the round GOOD with FAILED, as the 2560's
 * swap back does (tfw_boot_trial_fall_back). Nothing is ever swapped in:
 * boot_stage.c's copy already put the staged image in the region.
 *
 * PREV is laid out as the region itself, its trailer at the end, so the
 * store's own copy loop restores it with the image's body the whole region
 * below the trailer.
 *
 * The block is stored after the copy, never before: a reset mid-copy leaves
 * TRIAL or PENDING, and the next reset copies PREV again from the start. The
 * restored image is not trusted for having run before: main() evaluates it
 * as every boot does and, refused, stops in recovery -- on these parts
 * recovery_halt.c, for the programmer. A second fall-back is refused by the
 * trial's own guard: the block is no longer on trial or pending.
 */
#include "tfw/boot.h"
#include "boot_stage.h"
#include "boot_store.h"
#include "boot_swap.h"
#include "boot_trial.h"

#define PREV ((uint32_t)TFW_OTA_STORE_PREV_ADDR)

tfw_status_t tfw_boot_swap_fall_back(tfw_boot_platform_t *p)
{
    /* In RAM only, until PREV is back: a copy that fails stores nothing, and
     * main() then halts with the block as it was found. */
    tfw_status_t st = tfw_boot_trial_fall_back(p);

    if (st == TFW_OK) {
        st = tfw_boot_stage_copy(PREV, TFW_BOOT_MAX_IMAGE_LEN);
    }
    if (st == TFW_OK) {
        tfw_boot_platform_store(p);
    }
    return st;
}

tfw_boot_reset_action_t tfw_boot_swap_at_reset(tfw_boot_platform_t *p,
                                               tfw_boot_reset_action_t act)
{
    if (act == TFW_BOOT_RESET_FAIL && tfw_boot_swap_fall_back(p) == TFW_OK) {
        act = TFW_BOOT_RESET_BOOT;
    }
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
