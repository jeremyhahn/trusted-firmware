/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "writer.h"

void ag_writer_init(ag_writer_t *w, uint8_t *buf, uint8_t cap)
{
    w->buf      = buf;
    w->cap      = cap;
    w->pos      = 0u;
    w->overflow = 0u;
}

uint8_t ag_writer_ok(const ag_writer_t *w)
{
    return (uint8_t)(w->overflow == 0u);
}

uint8_t ag_writer_len(const ag_writer_t *w)
{
    return w->pos;
}

uint8_t ag_writer_remaining(const ag_writer_t *w)
{
    return (uint8_t)(w->pos <= w->cap ? (uint8_t)(w->cap - w->pos) : 0u);
}

/* Reserves n bytes and returns where to put them, or NULL if they do not fit.
 * The single place the bound is tested. The comparison avoids overflowing
 * pos + n, which on a uint8_t cursor is a real case rather than a theoretical
 * one: cap can be 128 and n can be 200. */
static uint8_t *give(ag_writer_t *w, uint8_t n)
{
    uint8_t *p;

    if (w->overflow) {
        return 0;
    }
    if (n > (uint8_t)(w->cap - w->pos)) {
        w->overflow = 1u;
        return 0;
    }
    p = &w->buf[w->pos];
    w->pos = (uint8_t)(w->pos + n);
    return p;
}

void ag_write_u8(ag_writer_t *w, uint8_t v)
{
    uint8_t *p = give(w, 1u);
    if (p) {
        p[0] = v;
    }
}

void ag_write_u16(ag_writer_t *w, uint16_t v)
{
    uint8_t *p = give(w, 2u);
    if (p) {
        p[0] = (uint8_t)(v & 0xFFu);
        p[1] = (uint8_t)((v >> 8) & 0xFFu);
    }
}

void ag_write_u32(ag_writer_t *w, uint32_t v)
{
    uint8_t *p = give(w, 4u);
    if (p) {
        p[0] = (uint8_t)(v & 0xFFu);
        p[1] = (uint8_t)((v >> 8) & 0xFFu);
        p[2] = (uint8_t)((v >> 16) & 0xFFu);
        p[3] = (uint8_t)((v >> 24) & 0xFFu);
    }
}

void ag_write_blob(ag_writer_t *w, const uint8_t *src, uint8_t n)
{
    uint8_t *p = give(w, n);

    if (p) {
        for (uint8_t i = 0u; i < n; i++) {
            p[i] = src[i];
        }
    }
}
