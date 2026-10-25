/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_TMPL_H
#define TFW_CAPS_TMPL_H

#include <stdint.h>
#include "tfw/crypto.h"
#include "../crypto/sha256.h"

/*
 * The emit sink: one pass hashes, the other windows, and a document is
 * described once for both.
 *
 * A signed document here is 365 to 490 bytes and an element-backed 328P image
 * has 239 bytes of its RAM budget left, so none is ever held whole. The bytes
 * are generated afresh for every window -- once to be signed, once per page to
 * be sent -- which costs time on a device that is idle between frames anyway,
 * and costs no RAM at all.
 */
typedef struct {
    tfw_sha256_t *hash;   /* the signing pass */
    uint8_t     *out;    /* the windowing pass */
    uint16_t     from;   /* first offset the window wants */
    uint8_t      cap;    /* how many bytes it can take */
    uint16_t     pos;    /* offsets emitted so far; the total, at the end */
    uint8_t      got;    /* bytes placed in the window */
} tfw_emit_t;

void tfw_emit_put(tfw_emit_t *e, uint8_t b);

/*
 * A TEMPLATE IS A TABLE OF STEPS, each one "n bytes of source s, from offset
 * off". The fixed bytes are a source like any other -- a flash array -- so a
 * hole is not a range test against an offset but simply a step whose source is
 * in RAM. Two documents can then share fixed bytes by naming the same flash
 * array, which is how the CSR reads its SubjectPublicKeyInfo and evidence
 * headers out of the alias certificate's template instead of carrying copies.
 *
 * The table lives in flash and carries no pointers, only a source index: the
 * caller hands over the pointers, so one table describes the document whatever
 * values fill it. Every width is a constant, which is what lets the DER lengths
 * around the holes be literals.
 *
 * `src` packs the kind of read into the high nibble and the source index into
 * the low one.
 */
typedef struct {
    uint8_t src;   /* TFW_TMPL_<kind> | source index */
    uint8_t off;   /* first byte of the source this step reads */
    uint8_t n;     /* bytes of the source it reads */
} tfw_tmpl_step_t;

#define TFW_TMPL_PGM  0x00u  /* flash, verbatim */
#define TFW_TMPL_RAM  0x10u  /* RAM, verbatim */
/* RAM, each byte as two lowercase hex digits: n source bytes emit 2n. */
#define TFW_TMPL_HEX  0x20u
/* RAM, each byte with its 0x40 bit set and its top bit clear -- the leading
 * byte of a DER INTEGER that must be positive and minimally encoded at a fixed
 * width, whatever value fills it. */
#define TFW_TMPL_POS  0x30u
#define TFW_TMPL_KIND 0xF0u
#define TFW_TMPL_SRC  0x0Fu

void tfw_tmpl_emit(tfw_emit_t *e, const tfw_tmpl_step_t *steps, uint8_t count,
                   const uint8_t *const *src);

/* SHA-256 over the steps' bytes: the hashing pass on its own. */
void tfw_tmpl_digest(const tfw_tmpl_step_t *steps, uint8_t count,
                     const uint8_t *const *src, uint8_t out[TFW_SHA256_LEN]);

#endif /* TFW_CAPS_TMPL_H */
