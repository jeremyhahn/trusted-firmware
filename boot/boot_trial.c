/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The one trial. See boot_trial.h; the machine is drawn in
 * docs/ota/architecture.md#device-state-machine.
 */

#include <string.h>

#include "boot_trial.h"
#include "boot_image.h"

tfw_boot_reset_action_t tfw_boot_trial_at_reset(const tfw_boot_platform_t *p)
{
    if (p->boot_state == TFW_BOOT_STATE_TRIAL) {
        return TFW_BOOT_RESET_FAIL;
    }
    if (p->boot_state == TFW_BOOT_STATE_CONFIRMED) {
        return TFW_BOOT_RESET_SETTLE;
    }
    if (p->ota_state == TFW_BOOT_OTA_PENDING) {
        return TFW_BOOT_RESET_TRY;
    }
    return TFW_BOOT_RESET_BOOT;
}

tfw_status_t tfw_boot_trial_arm(tfw_boot_platform_t *p)
{
    if (p->ota_state != TFW_BOOT_OTA_PENDING
        || p->boot_state == TFW_BOOT_STATE_TRIAL
        || p->boot_state == TFW_BOOT_STATE_CONFIRMED) {
        return TFW_ERR_BAD_STATE;
    }
    p->boot_state = TFW_BOOT_STATE_TRIAL;
    p->ota_state  = TFW_BOOT_OTA_IDLE;
    return TFW_OK;
}

tfw_status_t tfw_boot_trial_fail(tfw_boot_platform_t *p)
{
    if (p->boot_state != TFW_BOOT_STATE_TRIAL
        && p->ota_state != TFW_BOOT_OTA_PENDING) {
        return TFW_ERR_BAD_STATE;
    }
    p->boot_state = TFW_BOOT_STATE_TRIAL;   /* ended: at_reset fails it again */
    p->ota_state  = TFW_BOOT_OTA_FAILED;
    return TFW_OK;
}

tfw_status_t tfw_boot_trial_settle(tfw_boot_platform_t *p, uint32_t version,
                                   uint32_t floor, uint32_t *next)
{
    *next = floor;
    if (p->boot_state != TFW_BOOT_STATE_CONFIRMED) {
        return TFW_ERR_BAD_STATE;
    }
    p->boot_state = TFW_BOOT_STATE_GOOD;
    return tfw_boot_floor_advance(version, floor, next);
}

tfw_status_t tfw_boot_trial_confirm(tfw_boot_platform_t *p)
{
    if (p->boot_state != TFW_BOOT_STATE_TRIAL
        || p->ota_state == TFW_BOOT_OTA_FAILED) {
        return TFW_ERR_BAD_STATE;
    }
    p->boot_state = TFW_BOOT_STATE_CONFIRMED;
    return TFW_OK;
}

tfw_status_t tfw_boot_trial_activate(tfw_boot_platform_t *p,
                                     const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                     const uint8_t *failed)
{
    if (p->ota_state != TFW_BOOT_OTA_VERIFIED
        || p->boot_state == TFW_BOOT_STATE_TRIAL) {
        return TFW_ERR_BAD_STATE;
    }
    if (failed && memcmp(measurement, failed, TFW_BOOT_MEASURE_LEN) == 0) {
        return TFW_ERR_BAD_STATE;   /* the same image gets no second trial */
    }
    p->ota_state = TFW_BOOT_OTA_PENDING;
    return TFW_OK;
}

tfw_status_t tfw_boot_trial_begin_permit(const tfw_boot_platform_t *p)
{
    /* CONFIRMED is refused as well: the first page write would replace it with
     * STAGED, and the confirmed version's floor advance would be lost before it
     * was made. Recovery can hold CONFIRMED when the settle could not run (the
     * floor could not be read). The block is not written here, so a reset
     * leaves CONFIRMED and settles it again; a transfer is accepted once that
     * settle has written GOOD. */
    if (p->ota_state == TFW_BOOT_OTA_PENDING
        || p->boot_state == TFW_BOOT_STATE_CONFIRMED) {
        return TFW_ERR_BAD_STATE;
    }
    return TFW_OK;
}
