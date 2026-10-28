/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The record's store (docs/hsm-self-defence/m7-design.md slice 11a).
 *
 * Substituted at link time, never through a vtable: an image links exactly
 * one implementation. Today that is hsm_store_ram.c, which survives nothing.
 * The store that survives reset is architecture.md question 12 (m7-design.md
 * O4), and lands as another file beside it (slice 11b).
 *
 * The record puts an entry before the head that counts it, so a store that
 * loses power between the two loses that entry and keeps a whole record.
 */

#ifndef TFW_CORE_HSM_STORE_H
#define TFW_CORE_HSM_STORE_H

#include <stdint.h>

#include "hsm_record.h"
#include "tfw/status.h"

typedef struct {
    uint8_t  n;       /* entries kept */
    uint8_t  spare;   /* zero */
    uint16_t dropped; /* entries refused for want of room */
} tfw_hsm_store_head_t;

/* The head; all zero for a store never written or just erased. */
void tfw_hsm_store_head_get(tfw_hsm_store_head_t *out);
tfw_status_t tfw_hsm_store_head_put(const tfw_hsm_store_head_t *head);

/* Slot `slot`, below TFW_HSM_RECORD_MAX; TFW_ERR_RANGE past it. */
tfw_status_t tfw_hsm_store_get(uint8_t slot, tfw_hsm_entry_t *out);
tfw_status_t tfw_hsm_store_put(uint8_t slot, const tfw_hsm_entry_t *e);

/* The head and every slot back to zero. */
void tfw_hsm_store_erase(void);

#endif
