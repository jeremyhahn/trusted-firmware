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

/* suit-common: one component and the shared sequence's four parameters. 0 on
 * an overflow of the shared sequence. */
static size_t common(buf_t *c, const uint8_t image_digest[32], uint32_t image_size,
                   const uint8_t vendor[16], const uint8_t class_id[16])
{
    static const uint8_t component[1] = { 0x00u };
    uint8_t dg[36], shared[128];
    buf_t   d = { dg, 0u, sizeof dg };
    buf_t   s = { shared, 0u, sizeof shared };

    suit_digest(&d, image_digest);

    head(&s, 4u, 2u);
    head(&s, 0u, 20u);                 /* override-parameters */
    head(&s, 5u, 4u);
    head(&s, 0u, 1u);  bstr(&s, vendor, 16u);
    head(&s, 0u, 2u);  bstr(&s, class_id, 16u);
    head(&s, 0u, 3u);  bstr(&s, dg, d.n);
    head(&s, 0u, 14u); head(&s, 0u, image_size);

    head(c, 5u, 2u);
    head(c, 0u, 2u);
    head(c, 4u, 1u);
    head(c, 4u, 1u);
    bstr(c, component, sizeof component);
    if (done(&s) == 0u) {
        return 0u;
    }
    head(c, 0u, 4u);  bstr(c, shared, s.n);
    return done(c);
}

size_t tfw_suit_emit_manifest(uint8_t *out, size_t cap,
                              const uint8_t image_digest[32], uint32_t image_size,
                              uint32_t seq, const uint8_t vendor[16],
                              const uint8_t class_id[16])
{
    uint8_t common_b[160], man[200];
    buf_t   c = { common_b, 0u, sizeof common_b };
    buf_t   m = { man, 0u, sizeof man };
    buf_t   o = { out, 0u, cap };

    if (common(&c, image_digest, image_size, vendor, class_id) == 0u) {
        return 0u;
    }

    head(&m, 5u, 3u);
    head(&m, 0u, 1u);  head(&m, 0u, 1u);
    head(&m, 0u, 2u);  head(&m, 0u, seq);
    head(&m, 0u, 3u);  bstr(&m, common_b, c.n);

    if (done(&c) == 0u || done(&m) == 0u) {
        return 0u;
    }
    bstr(&o, man, m.n);
    return done(&o);
}

size_t tfw_suit_emit_manifest_integrated(uint8_t *out, size_t cap,
                                         const uint8_t payload_digest[32],
                                         uint32_t payload_size, uint32_t seq,
                                         const uint8_t vendor[16],
                                         const uint8_t class_id[16], const char *uri)
{
    size_t  un = strlen(uri);
    uint8_t common_b[160], inst[64], man[260];
    buf_t   c = { common_b, 0u, sizeof common_b };
    buf_t   i = { inst, 0u, sizeof inst };
    buf_t   m = { man, 0u, sizeof man };
    buf_t   o = { out, 0u, cap };

    if (common(&c, payload_digest, payload_size, vendor, class_id) == 0u) {
        return 0u;
    }

    head(&i, 4u, 6u);
    head(&i, 0u, 20u);                 /* override-parameters */
    head(&i, 5u, 1u);
    head(&i, 0u, 21u);                 /* uri */
    head(&i, 3u, (uint32_t)un);
    put(&i, (const uint8_t *)uri, un);
    head(&i, 0u, 21u);  head(&i, 0u, 15u);   /* fetch, report all */
    head(&i, 0u, 3u);   head(&i, 0u, 15u);   /* image match, report all */

    head(&m, 5u, 4u);
    head(&m, 0u, 1u);  head(&m, 0u, 1u);
    head(&m, 0u, 2u);  head(&m, 0u, seq);
    head(&m, 0u, 3u);  bstr(&m, common_b, c.n);
    head(&m, 0u, 20u); bstr(&m, inst, i.n);  /* suit-install */

    if (done(&c) == 0u || done(&i) == 0u || done(&m) == 0u) {
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

/* The authentication wrapper: the manifest digest and one detached
 * COSE_Sign1 around a 64-byte r||s signature. 0 on an overflow. */
static size_t auth(uint8_t out[128], const uint8_t *manifest, size_t manifest_len,
                   const uint8_t sig[64])
{
    static const uint8_t es256[3] = { 0xA1u, 0x01u, 0x26u };
    uint8_t pl[36], cose[80];
    size_t  pn = payload(pl, manifest, manifest_len);
    buf_t   k  = { cose, 0u, sizeof cose };
    buf_t   a  = { out, 0u, 128u };

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

    return done(&k) == 0u ? 0u : done(&a);
}

size_t tfw_suit_emit_envelope(uint8_t *out, size_t cap,
                              const uint8_t *manifest, size_t manifest_len,
                              const uint8_t sig[64])
{
    uint8_t a[128];
    size_t  an = auth(a, manifest, manifest_len, sig);
    buf_t   o  = { out, 0u, cap };

    if (an == 0u) {
        return 0u;
    }
    head(&o, 6u, 107u);                /* SUIT_Envelope_Tagged */
    head(&o, 5u, 2u);
    head(&o, 0u, 2u);
    bstr(&o, a, an);
    head(&o, 0u, 3u);
    put(&o, manifest, manifest_len);
    return done(&o);
}

size_t tfw_suit_emit_envelope_integrated(uint8_t *out, size_t cap,
                                         const uint8_t *manifest, size_t manifest_len,
                                         const uint8_t sig[64], const char *uri,
                                         const uint8_t *body, size_t body_len)
{
    uint8_t a[128];
    size_t  an = auth(a, manifest, manifest_len, sig);
    size_t  un = strlen(uri);
    buf_t   o  = { out, 0u, cap };

    if (an == 0u) {
        return 0u;
    }
    head(&o, 6u, 107u);                /* SUIT_Envelope_Tagged */
    head(&o, 5u, 3u);
    head(&o, 0u, 2u);
    bstr(&o, a, an);
    head(&o, 0u, 3u);
    put(&o, manifest, manifest_len);
    head(&o, 3u, (uint32_t)un);        /* the payload, under its URI */
    put(&o, (const uint8_t *)uri, un);
    bstr(&o, body, body_len);
    return done(&o);
}
