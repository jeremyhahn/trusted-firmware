/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * A6, the record: an append-only log over hsm_store.h. hsm_record.h states
 * what it keeps and what it does when full.
 */

#include "hsm_record.h"
#include "hsm_store.h"

/* A head past the bound (a store written by something else) reads as full,
 * never as an index past the slots. */
static uint8_t kept(const tfw_hsm_store_head_t *head)
{
    return head->n < TFW_HSM_RECORD_MAX ? head->n : (uint8_t)TFW_HSM_RECORD_MAX;
}

tfw_status_t tfw_hsm_record_append(uint8_t kind, uint8_t what, uint8_t result, uint32_t at)
{
    tfw_hsm_store_head_t head;
    tfw_hsm_entry_t      e;
    tfw_status_t         st;

    if (kind != TFW_HSM_REC_SIGNAL && kind != TFW_HSM_REC_DECISION &&
        kind != TFW_HSM_REC_ACTION) {
        return TFW_ERR_UNSUPPORTED;
    }
    st = tfw_hsm_store_head_get(&head);
    if (st != TFW_OK) {
        return st;
    }
    if (kept(&head) >= TFW_HSM_RECORD_MAX) {
        if (head.dropped < TFW_HSM_RECORD_DROPPED_MAX) {
            head.dropped++;
            (void)tfw_hsm_store_head_put(&head);
        }
        return TFW_ERR_NO_SLOT;
    }
    e.at     = at;
    e.kind   = kind;
    e.what   = what;
    e.result = result;
    e.spare  = 0u;
    /* The entry, then the head that counts it (hsm_store.h). */
    st = tfw_hsm_store_put(head.n, &e);
    if (st != TFW_OK) {
        return st;
    }
    head.n++;
    return tfw_hsm_store_head_put(&head);
}

uint8_t tfw_hsm_record_count(void)
{
    tfw_hsm_store_head_t head;

    if (tfw_hsm_store_head_get(&head) != TFW_OK) {
        return 0u;
    }
    return kept(&head);
}

uint16_t tfw_hsm_record_dropped(void)
{
    tfw_hsm_store_head_t head;

    if (tfw_hsm_store_head_get(&head) != TFW_OK) {
        return 0u;
    }
    return head.dropped;
}

tfw_status_t tfw_hsm_record_read(uint8_t i, tfw_hsm_entry_t *out)
{
    tfw_hsm_store_head_t head;
    tfw_status_t         st;

    st = tfw_hsm_store_head_get(&head);
    if (st != TFW_OK) {
        return st;
    }
    if (i >= kept(&head)) {
        return TFW_ERR_RANGE;
    }
    return tfw_hsm_store_get(i, out);
}

tfw_status_t tfw_hsm_record_erase(void)
{
    return tfw_hsm_store_erase();
}
