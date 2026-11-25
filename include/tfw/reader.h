/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A bounded cursor over a request payload.
 *
 * Every read is range-checked against the payload length, and the failure is
 * STICKY: once a read runs past the end, the cursor stays failed and every
 * later read fails too. That is the whole point. A command handler pulls its
 * arguments out in sequence and asks once, at the end, whether the sequence
 * was valid -- instead of testing each read and getting the test wrong on the
 * fifth of six. A handler that forgets to ask still cannot corrupt anything,
 * because a failed read yields zero rather than whatever was past the buffer.
 *
 * Scalars come back little-endian, assembled by shift and mask rather than
 * read through a cast, so the code is correct on a host of either endianness
 * and the compiler cannot decide the buffer is suitably aligned when it is not.
 */
#ifndef TFW_READER_H
#define TFW_READER_H

#include <stdint.h>

typedef struct {
    const uint8_t *buf;
    uint8_t        len;
    uint8_t        pos;
    uint8_t        underflow; /* sticky: set once, never cleared */
} tfw_reader_t;

void tfw_reader_init(tfw_reader_t *r, const uint8_t *buf, uint8_t len);

/* On underflow each of these returns 0 and sets the sticky flag. Zero is a
 * legitimate value, so the return alone never proves success -- ask
 * tfw_reader_ok() before acting on what was read. */
uint8_t  tfw_read_u8(tfw_reader_t *r);
uint16_t tfw_read_u16(tfw_reader_t *r);
uint32_t tfw_read_u32(tfw_reader_t *r);

/* Returns a pointer into the payload and advances by n, or NULL on underflow.
 * The pointer is only valid while the frame buffer is; nothing is copied. */
const uint8_t *tfw_read_blob(tfw_reader_t *r, uint8_t n);

uint8_t tfw_reader_remaining(const tfw_reader_t *r);

/* True when every read so far stayed in bounds. */
uint8_t tfw_reader_ok(const tfw_reader_t *r);

#endif /* TFW_READER_H */
