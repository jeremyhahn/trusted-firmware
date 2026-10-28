/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * A6, the record (docs/hsm-self-defence/m7-design.md slice 11a): what the
 * engine saw, what it decided and what it did, with each action's result, for
 * go-trusted-platform to read after recovery.
 *
 * An append-only log of fixed-size entries, kept through hsm_store.h. When it
 * is full it keeps the entries it holds and refuses the rest, counting them:
 * an attacker who can raise signals cannot push the first ones out, and the
 * count says how much went unrecorded.
 *
 * Which store survives reset is architecture.md question 12; who reads the
 * record, and by which command id, is m7-design.md question O9. Neither is
 * decided here: no command reads the record yet.
 */

#ifndef TFW_CORE_HSM_RECORD_H
#define TFW_CORE_HSM_RECORD_H

#include <stdint.h>

#include "tfw/status.h"

/* The record's bound, the engine's own (not the durable store's size, which
 * waits on architecture.md question 12). */
#define TFW_HSM_RECORD_MAX 32u

/* An entry's kind. Zero is never written: a slot that reads zero was never
 * kept. They extend, never renumber. */
#define TFW_HSM_REC_SIGNAL   1u /* `what` a signal, TFW_HSM_SIG_* */
#define TFW_HSM_REC_DECISION 2u /* `what` the verdict V the engine decided on */
#define TFW_HSM_REC_ACTION   3u /* `what` an action, TFW_HSM_ACT_*; `result` its status */

/* The dropped count stops here rather than wrapping to look like none. */
#define TFW_HSM_RECORD_DROPPED_MAX 0xFFFFu

typedef struct {
    uint32_t at;     /* the engine's clock, as tfw_hsm_signal's `now` */
    uint8_t  kind;   /* TFW_HSM_REC_* */
    uint8_t  what;
    uint8_t  result; /* an action's tfw_status_t; TFW_OK for the other kinds */
    uint8_t  spare;  /* zero */
} tfw_hsm_entry_t;

/* Appends one entry at `at`.
 *
 *   TFW_ERR_UNSUPPORTED  a kind not above, and nothing kept
 *   TFW_ERR_NO_SLOT      the record full: nothing kept, the dropped count up
 *   as the store's put */
tfw_status_t tfw_hsm_record_append(uint8_t kind, uint8_t what, uint8_t result, uint32_t at);

/* Entries kept, oldest first, and entries refused for want of room. */
uint8_t  tfw_hsm_record_count(void);
uint16_t tfw_hsm_record_dropped(void);

/* Entry `i`, 0 the oldest. TFW_ERR_RANGE, and `out` untouched, at or past
 * the count. */
tfw_status_t tfw_hsm_record_read(uint8_t i, tfw_hsm_entry_t *out);

/* Empties the record and its dropped count. No command reaches it (O9). */
void tfw_hsm_record_erase(void);

#endif
