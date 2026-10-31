/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The record's store in RAM. It survives nothing: a reset or a power loss
 * empties it, which is the record's whole weakness until architecture.md
 * question 12 names a store that survives (m7-design.md slice 11b).
 */

#include <string.h>

#include "hsm_store.h"

static tfw_hsm_store_head_t g_head;
static tfw_hsm_entry_t      g_slot[TFW_HSM_RECORD_MAX];

tfw_status_t tfw_hsm_store_head_get(tfw_hsm_store_head_t *out)
{
    *out = g_head;
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_head_put(const tfw_hsm_store_head_t *head)
{
    g_head = *head;
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_get(uint8_t slot, tfw_hsm_entry_t *out)
{
    if (slot >= TFW_HSM_RECORD_MAX) {
        return TFW_ERR_RANGE;
    }
    *out = g_slot[slot];
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_put(uint8_t slot, const tfw_hsm_entry_t *e)
{
    if (slot >= TFW_HSM_RECORD_MAX) {
        return TFW_ERR_RANGE;
    }
    g_slot[slot] = *e;
    return TFW_OK;
}

tfw_status_t tfw_hsm_store_erase(void)
{
    memset(&g_head, 0, sizeof g_head);
    memset(g_slot, 0, sizeof g_slot);
    return TFW_OK;
}
