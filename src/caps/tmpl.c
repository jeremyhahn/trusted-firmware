/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The template walker both device-built documents are emitted through: the
 * alias certificate's TBSCertificate (src/caps/alias.c) and the possession
 * CSR's CertificationRequestInfo (src/caps/csr.c).
 *
 * ONE WALKER, NOT TWO, because the property both documents rest on is the same
 * and is easier to keep once: every byte the DeviceID key signs comes from a
 * flash constant or from a fixed-width value this firmware chose, and the walk
 * is the only code that puts those bytes in order. A host chooses none of the
 * steps, none of the widths and none of the offsets.
 */

#include <string.h>
#include "tmpl.h"
#include "tfw/progmem.h"

void tfw_emit_put(tfw_emit_t *e, uint8_t b)
{
    if (e->hash) {
        tfw_sha256_update(e->hash, &b, 1u);
    }
    if (e->out && e->pos >= e->from && e->got < e->cap) {
        e->out[e->got++] = b;
    }
    e->pos++;
}

static uint8_t hex_digit(uint8_t v)
{
    return (uint8_t)(v + (v < 10u ? (unsigned)'0' : (unsigned)'a' - 10u));
}

void tfw_tmpl_emit(tfw_emit_t *e, const tfw_tmpl_step_t *steps, uint8_t count,
                   const uint8_t *const *src)
{
    uint8_t k;

    for (k = 0u; k < count; k++) {
        uint8_t        how  = tfw_pgm_u8(&steps[k].src);
        uint8_t        kind = (uint8_t)(how & TFW_TMPL_KIND);
        uint8_t        n    = tfw_pgm_u8(&steps[k].n);
        const uint8_t *p    = src[how & TFW_TMPL_SRC] + tfw_pgm_u8(&steps[k].off);

        for (; n > 0u; n--, p++) {
            uint8_t b = kind == TFW_TMPL_PGM ? tfw_pgm_u8(p) : *p;

            if (kind == TFW_TMPL_HEX) {
                tfw_emit_put(e, hex_digit((uint8_t)(b >> 4)));
                b = hex_digit((uint8_t)(b & 0x0Fu));
            } else if (kind == TFW_TMPL_POS) {
                b = (uint8_t)((b | 0x40u) & 0x7Fu);
            }
            tfw_emit_put(e, b);
        }
    }
}

void tfw_tmpl_digest(const tfw_tmpl_step_t *steps, uint8_t count,
                     const uint8_t *const *src, uint8_t out[TFW_SHA256_LEN])
{
    tfw_sha256_t c;
    tfw_emit_t   e;

    memset(&e, 0, sizeof e);
    tfw_sha256_init(&c);
    e.hash = &c;
    tfw_tmpl_emit(&e, steps, count, src);
    tfw_sha256_final(&c, out);
}
