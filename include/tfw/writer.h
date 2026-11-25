/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A bounded cursor over a response payload.
 *
 * This type exists because of a specific defect in the firmware being
 * replaced. That code built its JSON reply with a chain of strcat() calls into
 * a fixed char[375], with no check at any step; a longer-than-expected sensor
 * reading walked off the end of the buffer and into whatever followed it in
 * SRAM. The failure was silent, data-dependent, and would have been invisible
 * in testing right up until the day it was not.
 *
 * So: every write is range-checked, and overflow is STICKY. A handler emits
 * its response fields in sequence and the dispatcher asks once, at the end,
 * whether they all fit -- turning what used to be a memory-corrupting bug into
 * an TFW_ERR_TOO_LONG status the host can act on.
 *
 * An overflowing write stores NOTHING. It does not fill the remaining space
 * and truncate, because a half-written field is a well-formed frame carrying
 * a wrong value, which is worse than a frame that says it failed.
 *
 * Scalars go out little-endian, by shift and mask.
 */
#ifndef TFW_WRITER_H
#define TFW_WRITER_H

#include <stdint.h>

typedef struct {
    uint8_t *buf;
    uint8_t  cap;
    uint8_t  pos;
    uint8_t  overflow; /* sticky: set once, never cleared */
} tfw_writer_t;

void tfw_writer_init(tfw_writer_t *w, uint8_t *buf, uint8_t cap);

void tfw_write_u8(tfw_writer_t *w, uint8_t v);
void tfw_write_u16(tfw_writer_t *w, uint16_t v);
void tfw_write_u32(tfw_writer_t *w, uint32_t v);
void tfw_write_blob(tfw_writer_t *w, const uint8_t *src, uint8_t n);

/* Bytes written so far. Meaningful only when tfw_writer_ok() is true; after an
 * overflow it reports what was committed before the first refusal, which is
 * not a frame anyone should send. */
uint8_t tfw_writer_len(const tfw_writer_t *w);

uint8_t tfw_writer_remaining(const tfw_writer_t *w);

/* True when every write so far fitted. */
uint8_t tfw_writer_ok(const tfw_writer_t *w);

#endif /* TFW_WRITER_H */
