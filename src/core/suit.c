/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SUIT manifest reader and activation gate. The profile and why it lives
 * in the application are in suit.h.
 *
 * No general CBOR decoder: every member is expected in the profile's order, so
 * the reader is a straight line of "this head, then this one", with no
 * recursion and no stack that grows with the input. Every length is checked
 * against what remains before anything is read past it.
 */
#include <string.h>

#include "suit.h"
#include "suit_cbor.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/progmem.h"

#define MT_UINT   TFW_SUIT_MT_UINT
#define MT_NINT   TFW_SUIT_MT_NINT
#define MT_BSTR   TFW_SUIT_MT_BSTR
#define MT_ARRAY  TFW_SUIT_MT_ARRAY
#define MT_MAP    TFW_SUIT_MT_MAP
#define MT_TAG    TFW_SUIT_MT_TAG
#define MT_SIMPLE TFW_SUIT_MT_SIMPLE

#define TAG_SUIT_ENVELOPE   TFW_SUIT_TAG_ENVELOPE
#define TAG_COSE_SIGN1      18u
#define COSE_ALG_SHA256     15u   /* -16, as a negative integer's argument */
#define SIMPLE_NIL          22u

#define ENV_AUTH            TFW_SUIT_ENV_AUTH
#define ENV_MANIFEST        TFW_SUIT_ENV_MANIFEST
#define MAN_VERSION         TFW_SUIT_MAN_VERSION
#define MAN_SEQUENCE        TFW_SUIT_MAN_SEQUENCE
#define MAN_COMMON          TFW_SUIT_MAN_COMMON
#define COMMON_COMPONENTS   2u
#define COMMON_SHARED_SEQ   4u
#define DIR_OVERRIDE_PARAMS 20u
#define PARAM_VENDOR_ID     1u
#define PARAM_CLASS_ID      2u
#define PARAM_IMAGE_DIGEST  3u
#define PARAM_IMAGE_SIZE    14u

#define SUIT_DIGEST_MAX     40u   /* [-16, bstr(32)] is 36 */
#define COMPONENT_PARTS_MAX 4u
#define COMPONENT_PART_MAX  16u
#define KID_MAX             32u

typedef tfw_suit_cur_t cur_t;

/* One CBOR head: major type and argument. 8-byte arguments are out of range
 * for every member here; indefinite lengths and reserved values are not in the
 * profile. */
tfw_status_t tfw_suit_head(cur_t *c, uint8_t *major, uint32_t *val)
{
    uint8_t b;
    uint8_t ai;
    uint8_t k;

    if (c->n == 0u) {
        return TFW_ERR_RANGE;
    }
    b = *c->p++;
    c->n--;
    *major = (uint8_t)(b >> 5);
    ai     = (uint8_t)(b & 0x1Fu);
    if (ai < 24u) {
        *val = ai;
        return TFW_OK;
    }
    if (ai == 27u) {
        return TFW_ERR_RANGE;
    }
    if (ai > 27u) {
        return TFW_ERR_UNSUPPORTED;
    }
    k = (uint8_t)(1u << (ai - 24u));
    if (c->n < k) {
        return TFW_ERR_RANGE;
    }
    *val = 0u;
    while (k-- != 0u) {
        *val = (*val << 8) | *c->p++;
        c->n--;
    }
    return TFW_OK;
}

/* Exactly this head, or the input is not the profile. */
tfw_status_t tfw_suit_expect(cur_t *c, uint8_t major, uint32_t val)
{
    uint8_t      m;
    uint32_t     v;
    tfw_status_t st = tfw_suit_head(c, &m, &v);

    if (st != TFW_OK) {
        return st;
    }
    return (m == major && v == val) ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_suit_uint32(cur_t *c, uint32_t *out)
{
    uint8_t      m;
    tfw_status_t st = tfw_suit_head(c, &m, out);

    if (st != TFW_OK) {
        return st;
    }
    return m == MT_UINT ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

/* A byte or text string of at most `max` bytes: `sub` spans its content and
 * `c` moves past it. A length past what remains, or past `max`, is a range
 * refusal. */
tfw_status_t tfw_suit_string(cur_t *c, uint8_t major, uint16_t max, cur_t *sub)
{
    uint8_t      m;
    uint32_t     v;
    tfw_status_t st = tfw_suit_head(c, &m, &v);

    if (st != TFW_OK) {
        return st;
    }
    if (m != major) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (v > max || v > c->n) {
        return TFW_ERR_RANGE;
    }
    sub->p = c->p;
    sub->n = (uint16_t)v;
    c->p  += v;
    c->n   = (uint16_t)(c->n - v);
    return TFW_OK;
}

static tfw_status_t bstr(cur_t *c, uint16_t max, cur_t *sub)
{
    return tfw_suit_string(c, MT_BSTR, max, sub);
}

/* A byte string of exactly `len` bytes, copied out. */
static tfw_status_t bstr_fixed(cur_t *c, uint8_t *out, uint16_t len)
{
    cur_t        s;
    tfw_status_t st = bstr(c, len, &s);

    if (st != TFW_OK) {
        return st;
    }
    if (s.n != len) {
        return TFW_ERR_UNSUPPORTED;
    }
    memcpy(out, s.p, len);
    return TFW_OK;
}

/* Every container is read to its end: a byte left over is a member the
 * profile does not list. */
tfw_status_t tfw_suit_done(const cur_t *c)
{
    return c->n == 0u ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

/* SUIT_Digest: [-16, bstr(32)], filling the whole of `c`. */
static tfw_status_t suit_digest(cur_t c, uint8_t out[TFW_SUIT_DIGEST_LEN])
{
    tfw_status_t st = tfw_suit_expect(&c, MT_ARRAY, 2u);

    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_NINT, COSE_ALG_SHA256);
    if (st == TFW_OK) st = bstr_fixed(&c, out, TFW_SUIT_DIGEST_LEN);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}

/* COSE_Sign1_Tagged, payload detached: only ES256 in the protected header,
 * nothing or a key id in the unprotected one. The key id is not trusted for
 * anything -- the key is the board's slot -- so it is bounded and skipped. */
/* The constants this file matches against live in flash: on AVR a const array
 * without TFW_PROGMEM is copied into RAM at startup (#55). Nonzero
 * where ram[0..n) and pgm[0..n) differ; a null ram compares against zeros. */
static uint8_t pgm_differs(const uint8_t *ram, const uint8_t *pgm, uint8_t n)
{
    uint8_t i, diff = 0u;

    for (i = 0u; i < n; i++) {
        diff = (uint8_t)(diff | ((ram ? ram[i] : 0u) ^ tfw_pgm_u8(&pgm[i])));
    }
    return diff;
}

static tfw_status_t cose_sign1(cur_t c, uint8_t sig[TFW_SIG_LEN])
{
    static const uint8_t es256[3] TFW_PROGMEM = { 0xA1u, 0x01u, 0x26u };   /* {1: -7} */
    cur_t        s;
    uint8_t      m;
    uint32_t     v;
    tfw_status_t st = tfw_suit_expect(&c, MT_TAG, TAG_COSE_SIGN1);

    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_ARRAY, 4u);
    if (st == TFW_OK) st = bstr(&c, sizeof es256, &s);
    if (st == TFW_OK && (s.n != sizeof es256 || pgm_differs(s.p, es256, sizeof es256) != 0u)) {
        st = TFW_ERR_UNSUPPORTED;
    }
    if (st == TFW_OK) st = tfw_suit_head(&c, &m, &v);
    if (st == TFW_OK && (m != MT_MAP || v > 1u)) {
        st = TFW_ERR_UNSUPPORTED;
    }
    if (st == TFW_OK && v == 1u) {
        st = tfw_suit_expect(&c, MT_UINT, 4u);
        if (st == TFW_OK) st = bstr(&c, KID_MAX, &s);
    }
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_SIMPLE, SIMPLE_NIL);
    if (st == TFW_OK) st = bstr_fixed(&c, sig, TFW_SIG_LEN);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}

/* ES256 over Sig_structure ["Signature1", h'A10126', h'', payload]. */
static tfw_status_t tbs_hash(const cur_t *payload, uint8_t h[TFW_SHA256_LEN])
{
    static const uint8_t prefix[17] = {
        0x84u, 0x6Au, 'S', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '1',
        0x43u, 0xA1u, 0x01u, 0x26u, 0x40u
    };
    uint8_t  tbs[sizeof prefix + 2u + SUIT_DIGEST_MAX];
    uint16_t n = sizeof prefix;

    memcpy(tbs, prefix, sizeof prefix);
    if (payload->n < 24u) {
        tbs[n++] = (uint8_t)(0x40u | payload->n);
    } else {
        tbs[n++] = 0x58u;
        tbs[n++] = (uint8_t)payload->n;
    }
    memcpy(&tbs[n], payload->p, payload->n);
    n = (uint16_t)(n + payload->n);

    return tfw_crypto_sha256(tbs, n, h) == TFW_OK ? TFW_OK : TFW_ERR_CRYPTO;
}

/* The authentication wrapper: the manifest digest, then one signature. The
 * digest is checked here; the signature is the caller's, under its key. */
tfw_status_t tfw_suit_auth(cur_t auth, const cur_t *wrapped,
                           uint8_t tbs_h[TFW_SHA256_LEN], uint8_t sig[TFW_SIG_LEN])
{
    cur_t        dg;
    cur_t        cose;
    uint8_t      want[TFW_SUIT_DIGEST_LEN];
    uint8_t      have[TFW_SHA256_LEN];
    tfw_status_t st = tfw_suit_expect(&auth, MT_ARRAY, 2u);

    if (st == TFW_OK) st = bstr(&auth, SUIT_DIGEST_MAX, &dg);
    if (st == TFW_OK) st = suit_digest(dg, want);
    if (st == TFW_OK) st = bstr(&auth, TFW_SUIT_ENVELOPE_MAX, &cose);
    if (st == TFW_OK) st = cose_sign1(cose, sig);
    if (st == TFW_OK) st = tfw_suit_done(&auth);
    if (st != TFW_OK) {
        return st;
    }

    /* Authenticate before the manifest is read at all: the signature binds
     * the digest, and the digest binds the manifest's bytes, header included. */
    if (tfw_crypto_sha256(wrapped->p, wrapped->n, have) != TFW_OK
        || memcmp(have, want, TFW_SUIT_DIGEST_LEN) != 0) {
        return TFW_ERR_CRYPTO;
    }
    return tbs_hash(&dg, tbs_h);
}

/* The shared sequence: one override-parameters directive, four parameters. */
static tfw_status_t shared_seq(cur_t c, tfw_suit_manifest_t *m)
{
    cur_t        s;
    tfw_status_t st = tfw_suit_expect(&c, MT_ARRAY, 2u);

    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, DIR_OVERRIDE_PARAMS);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_MAP, 4u);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, PARAM_VENDOR_ID);
    if (st == TFW_OK) st = bstr_fixed(&c, m->vendor, TFW_SUIT_ID_LEN);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, PARAM_CLASS_ID);
    if (st == TFW_OK) st = bstr_fixed(&c, m->class_id, TFW_SUIT_ID_LEN);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, PARAM_IMAGE_DIGEST);
    if (st == TFW_OK) st = bstr(&c, SUIT_DIGEST_MAX, &s);
    if (st == TFW_OK) st = suit_digest(s, m->image_digest);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, PARAM_IMAGE_SIZE);
    if (st == TFW_OK) st = tfw_suit_uint32(&c, &m->image_size);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}

/* suit-common: one component, whose identifier is skipped -- this profile
 * has one image, so there is nothing for it to select -- and the shared
 * sequence. */
tfw_status_t tfw_suit_common(cur_t c, tfw_suit_manifest_t *m)
{
    cur_t        s;
    uint8_t      t;
    uint32_t     parts;
    tfw_status_t st = tfw_suit_expect(&c, MT_MAP, 2u);

    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, COMMON_COMPONENTS);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_ARRAY, 1u);
    if (st == TFW_OK) st = tfw_suit_head(&c, &t, &parts);
    if (st == TFW_OK && (t != MT_ARRAY || parts == 0u || parts > COMPONENT_PARTS_MAX)) {
        st = TFW_ERR_UNSUPPORTED;
    }
    while (st == TFW_OK && parts-- != 0u) {
        st = bstr(&c, COMPONENT_PART_MAX, &s);
    }
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, COMMON_SHARED_SEQ);
    if (st == TFW_OK) st = bstr(&c, TFW_SUIT_ENVELOPE_MAX, &s);
    if (st == TFW_OK) st = shared_seq(s, m);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}

static tfw_status_t manifest(cur_t c, tfw_suit_manifest_t *m)
{
    cur_t        s;
    tfw_status_t st = tfw_suit_expect(&c, MT_MAP, 3u);

    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, MAN_VERSION);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, 1u);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, MAN_SEQUENCE);
    if (st == TFW_OK) st = tfw_suit_uint32(&c, &m->seq);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, MAN_COMMON);
    if (st == TFW_OK) st = bstr(&c, TFW_SUIT_ENVELOPE_MAX, &s);
    if (st == TFW_OK) st = tfw_suit_common(s, m);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}

tfw_status_t tfw_suit_read(const uint8_t *env, uint16_t len, tfw_suit_manifest_t *out)
{
    tfw_suit_manifest_t m;
    cur_t               c;
    cur_t               auth;
    cur_t               man;
    cur_t               wrapped;
    uint8_t             h[TFW_SHA256_LEN];
    uint8_t             sig[TFW_SIG_LEN];
    tfw_status_t        st;

    if (len > TFW_SUIT_ENVELOPE_MAX) {
        return TFW_ERR_RANGE;
    }
    c.p = env;
    c.n = len;

    st = tfw_suit_expect(&c, MT_TAG, TAG_SUIT_ENVELOPE);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_MAP, 2u);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, ENV_AUTH);
    if (st == TFW_OK) st = bstr(&c, TFW_SUIT_ENVELOPE_MAX, &auth);
    if (st == TFW_OK) st = tfw_suit_expect(&c, MT_UINT, ENV_MANIFEST);
    wrapped.p = c.p;
    if (st == TFW_OK) st = bstr(&c, TFW_SUIT_ENVELOPE_MAX, &man);
    wrapped.n = (uint16_t)(c.p - wrapped.p);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    if (st == TFW_OK) st = tfw_suit_auth(auth, &wrapped, h, sig);
    if (st == TFW_OK) st = tfw_crypto_verify_image(h, sig);
    if (st != TFW_OK) {
        return st;
    }

    st = manifest(man, &m);
    if (st == TFW_OK) {
        *out = m;
    }
    return st;
}

static tfw_status_t hold(const tfw_suit_manifest_t *m, const tfw_boot_trailer_t *tr,
                         const uint8_t staged_digest[TFW_SUIT_DIGEST_LEN],
                         uint32_t staged_len)
{
    static const uint8_t vendor[TFW_SUIT_ID_LEN]   TFW_PROGMEM = TFW_SUIT_VENDOR_ID;
    static const uint8_t class_id[TFW_SUIT_ID_LEN] TFW_PROGMEM = TFW_SUIT_CLASS_ID;

    /* The binding: this project's vendor id, this board's class id, and a
     * trailer naming this board. A board with no class id binds nothing. */
    if (pgm_differs(m->vendor, vendor, TFW_SUIT_ID_LEN) != 0u
        || pgm_differs(0, class_id, TFW_SUIT_ID_LEN) == 0u
        || pgm_differs(m->class_id, class_id, TFW_SUIT_ID_LEN) != 0u
        || tr->board_id != TFW_BOARD_ID) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (memcmp(m->image_digest, staged_digest, TFW_SUIT_DIGEST_LEN) != 0
        || memcmp(m->image_digest, tr->measurement, TFW_SUIT_DIGEST_LEN) != 0) {
        return TFW_ERR_CRYPTO;
    }
    if (m->image_size != staged_len || m->image_size != tr->image_len) {
        return TFW_ERR_RANGE;
    }
    if (m->seq != tr->image_version) {
        return TFW_ERR_PROTECTED_REGION;
    }
    return TFW_OK;
}

tfw_status_t tfw_suit_gate(tfw_boot_platform_t *p, const tfw_boot_trailer_t *tr,
                           const uint8_t staged_digest[TFW_SUIT_DIGEST_LEN],
                           uint32_t staged_len, const uint8_t *env, uint16_t env_len)
{
    tfw_suit_manifest_t m;
    tfw_status_t        st = tfw_suit_read(env, env_len, &m);

    if (st == TFW_OK) {
        st = hold(&m, tr, staged_digest, staged_len);
    }
    p->ota_state = (st == TFW_OK) ? TFW_BOOT_OTA_VERIFIED : TFW_BOOT_OTA_FAILED;
    return st;
}
