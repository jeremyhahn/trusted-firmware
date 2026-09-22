/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "reader.h"

void ag_reader_init(ag_reader_t *r, const uint8_t *buf, uint8_t len)
{
    r->buf       = buf;
    r->len       = len;
    r->pos       = 0u;
    r->underflow = 0u;
}

uint8_t ag_reader_ok(const ag_reader_t *r)
{
    return (uint8_t)(r->underflow == 0u);
}

uint8_t ag_reader_remaining(const ag_reader_t *r)
{
    return (uint8_t)(r->pos <= r->len ? (uint8_t)(r->len - r->pos) : 0u);
}

/* Reserves n bytes and returns their address, or NULL if they are not there.
 * The single place the bound is tested, so there is one place for it to be
 * wrong. Note the comparison is written to avoid overflowing pos + n. */
static const uint8_t *take(ag_reader_t *r, uint8_t n)
{
    const uint8_t *p;

    if (r->underflow) {
        return 0;
    }
    if (n > (uint8_t)(r->len - r->pos)) {
        r->underflow = 1u;
        return 0;
    }
    p = &r->buf[r->pos];
    r->pos = (uint8_t)(r->pos + n);
    return p;
}

uint8_t ag_read_u8(ag_reader_t *r)
{
    const uint8_t *p = take(r, 1u);
    return p ? p[0] : 0u;
}

uint16_t ag_read_u16(ag_reader_t *r)
{
    const uint8_t *p = take(r, 2u);
    if (!p) {
        return 0u;
    }
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t ag_read_u32(ag_reader_t *r)
{
    const uint8_t *p = take(r, 4u);
    if (!p) {
        return 0u;
    }
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

const uint8_t *ag_read_blob(ag_reader_t *r, uint8_t n)
{
    return take(r, n);
}
