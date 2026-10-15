/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SUIT manifest emitter. The profile is src/core/suit.h's, written in the
 * same order the reader expects it: deterministic CBOR, shortest heads.
 */
#include <string.h>

#include "emit.h"
#include "sha256.h"

typedef struct {
    uint8_t *p;
    size_t   n;
    size_t   cap;
} buf_t;

static void put(buf_t *b, const uint8_t *d, size_t len)
{
    if (len != 0u && b->n + len <= b->cap) {
        memcpy(&b->p[b->n], d, len);
    }
    b->n += len;
}

static void put1(buf_t *b, uint8_t v)
{
    put(b, &v, 1u);
}

/* A CBOR head in its shortest form. */
static void head(buf_t *b, uint8_t major, uint32_t v)
{
    uint8_t mt = (uint8_t)(major << 5);

    if (v < 24u) {
        put1(b, (uint8_t)(mt | v));
    } else if (v <= 0xFFu) {
        put1(b, (uint8_t)(mt | 24u));
        put1(b, (uint8_t)v);
    } else if (v <= 0xFFFFu) {
        put1(b, (uint8_t)(mt | 25u));
        put1(b, (uint8_t)(v >> 8));
        put1(b, (uint8_t)v);
    } else {
        put1(b, (uint8_t)(mt | 26u));
        put1(b, (uint8_t)(v >> 24));
        put1(b, (uint8_t)(v >> 16));
        put1(b, (uint8_t)(v >> 8));
        put1(b, (uint8_t)v);
    }
}

static void bstr(buf_t *b, const uint8_t *d, size_t len)
{
    head(b, 2u, (uint32_t)len);
    put(b, d, len);
}

static size_t done(const buf_t *b)
{
    return b->n <= b->cap ? b->n : 0u;
}

/* SUIT_Digest: [-16, bstr(32)], 36 bytes. */
static void suit_digest(buf_t *b, const uint8_t d[32])
{
    head(b, 4u, 2u);
    head(b, 1u, 15u);
    bstr(b, d, 32u);
}

size_t tfw_suit_emit_manifest(uint8_t *out, size_t cap,
                              const uint8_t image_digest[32], uint32_t image_size,
                              uint32_t seq, const uint8_t vendor[16],
                              const uint8_t class_id[16])
{
    static const uint8_t component[1] = { 0x00u };
    uint8_t dg[36], shared[128], common[160], man[200];
    buf_t   d = { dg, 0u, sizeof dg };
    buf_t   s = { shared, 0u, sizeof shared };
    buf_t   c = { common, 0u, sizeof common };
    buf_t   m = { man, 0u, sizeof man };
    buf_t   o = { out, 0u, cap };

    suit_digest(&d, image_digest);

    head(&s, 4u, 2u);
    head(&s, 0u, 20u);                 /* override-parameters */
    head(&s, 5u, 4u);
    head(&s, 0u, 1u);  bstr(&s, vendor, 16u);
    head(&s, 0u, 2u);  bstr(&s, class_id, 16u);
    head(&s, 0u, 3u);  bstr(&s, dg, d.n);
    head(&s, 0u, 14u); head(&s, 0u, image_size);

    head(&c, 5u, 2u);
    head(&c, 0u, 2u);
    head(&c, 4u, 1u);
    head(&c, 4u, 1u);
    bstr(&c, component, sizeof component);
    head(&c, 0u, 4u);  bstr(&c, shared, s.n);

    head(&m, 5u, 3u);
    head(&m, 0u, 1u);  head(&m, 0u, 1u);
    head(&m, 0u, 2u);  head(&m, 0u, seq);
    head(&m, 0u, 3u);  bstr(&m, common, c.n);

    if (done(&s) == 0u || done(&c) == 0u || done(&m) == 0u) {
        return 0u;
    }
    bstr(&o, man, m.n);
    return done(&o);
}

/* The bstr-wrapped manifest digest: the Sig_structure's payload. */
static size_t payload(uint8_t out[36], const uint8_t *manifest, size_t manifest_len)
{
    tfw_sha256_t ctx;
    uint8_t      h[32];
    buf_t        p = { out, 0u, 36u };

    if (manifest_len > 0xFFFFu) {
        return 0u;
    }
    tfw_sha256_init(&ctx);
    tfw_sha256_update(&ctx, manifest, (uint16_t)manifest_len);
    tfw_sha256_final(&ctx, h);
    suit_digest(&p, h);
    return done(&p);
}

size_t tfw_suit_emit_tbs(uint8_t *out, size_t cap,
                         const uint8_t *manifest, size_t manifest_len)
{
    static const uint8_t es256[3] = { 0xA1u, 0x01u, 0x26u };
    uint8_t pl[36];
    size_t  pn = payload(pl, manifest, manifest_len);
    buf_t   o  = { out, 0u, cap };

    if (pn == 0u) {
        return 0u;
    }
    head(&o, 4u, 4u);
    head(&o, 3u, 10u);
    put(&o, (const uint8_t *)"Signature1", 10u);
    bstr(&o, es256, sizeof es256);
    bstr(&o, NULL, 0u);
    bstr(&o, pl, pn);
    return done(&o);
}

size_t tfw_suit_emit_envelope(uint8_t *out, size_t cap,
                              const uint8_t *manifest, size_t manifest_len,
                              const uint8_t sig[64])
{
    static const uint8_t es256[3] = { 0xA1u, 0x01u, 0x26u };
    uint8_t pl[36], cose[80], auth[128];
    size_t  pn = payload(pl, manifest, manifest_len);
    buf_t   k  = { cose, 0u, sizeof cose };
    buf_t   a  = { auth, 0u, sizeof auth };
    buf_t   o  = { out, 0u, cap };

    if (pn == 0u) {
        return 0u;
    }
    head(&k, 6u, 18u);                 /* COSE_Sign1_Tagged */
    head(&k, 4u, 4u);
    bstr(&k, es256, sizeof es256);
    head(&k, 5u, 0u);
    put1(&k, 0xF6u);                   /* nil: the payload is detached */
    bstr(&k, sig, 64u);

    head(&a, 4u, 2u);
    bstr(&a, pl, pn);
    bstr(&a, cose, k.n);

    if (done(&k) == 0u || done(&a) == 0u) {
        return 0u;
    }
    head(&o, 6u, 107u);                /* SUIT_Envelope_Tagged */
    head(&o, 5u, 2u);
    head(&o, 0u, 2u);
    bstr(&o, auth, a.n);
    head(&o, 0u, 3u);
    put(&o, manifest, manifest_len);
    return done(&o);
}
