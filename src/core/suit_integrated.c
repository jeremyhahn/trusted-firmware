/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SUIT reader for an envelope with its payload integrated, under a key the
 * caller names (docs/hsm-self-defence/adr-response-policy-format.md D1, D3,
 * D7). The profile is in suit.h. Beside tfw_suit_read rather than inside it,
 * so the image reader's links (the application's gate, the ESP32 layer 0)
 * carry none of it: a board gets this file only from its object list.
 */
#include <string.h>

#include "suit.h"
#include "suit_cbor.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/progmem.h"

#define MAN_INSTALL         20u
#define DIR_OVERRIDE_PARAMS 20u
#define DIR_FETCH           21u
#define COND_IMAGE_MATCH    3u
#define PARAM_URI           21u

/* suit-install: [20, {21: "#..."}, 21, policy, 3, policy] -- the URI, the
 * fetch from it, and the image match (draft-ietf-suit-manifest-37 §7.3,
 * §7.5). The report policies are read and not acted on. */
static tfw_status_t install(tfw_suit_cur_t c, tfw_suit_cur_t *uri)
{
    uint32_t     policy;
    tfw_status_t st = tfw_suit_expect(&c, TFW_SUIT_MT_ARRAY, 6u);

    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, DIR_OVERRIDE_PARAMS);
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_MAP, 1u);
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, PARAM_URI);
    if (st == TFW_OK) st = tfw_suit_string(&c, TFW_SUIT_MT_TSTR, TFW_SUIT_URI_MAX, uri);
    /* Fragment-only: the payload is in this envelope, nowhere else. */
    if (st == TFW_OK && (uri->n < 2u || uri->p[0] != (uint8_t)'#')) {
        st = TFW_ERR_UNSUPPORTED;
    }
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, DIR_FETCH);
    if (st == TFW_OK) st = tfw_suit_uint32(&c, &policy);
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, COND_IMAGE_MATCH);
    if (st == TFW_OK) st = tfw_suit_uint32(&c, &policy);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}

static tfw_status_t manifest(tfw_suit_cur_t c, tfw_suit_manifest_t *m, tfw_suit_cur_t *uri)
{
    tfw_suit_cur_t s;
    tfw_status_t   st = tfw_suit_expect(&c, TFW_SUIT_MT_MAP, 4u);

    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, TFW_SUIT_MAN_VERSION);
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, 1u);
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, TFW_SUIT_MAN_SEQUENCE);
    if (st == TFW_OK) st = tfw_suit_uint32(&c, &m->seq);
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, TFW_SUIT_MAN_COMMON);
    if (st == TFW_OK) st = tfw_suit_string(&c, TFW_SUIT_MT_BSTR, TFW_SUIT_ENVELOPE_MAX, &s);
    if (st == TFW_OK) st = tfw_suit_common(s, m);
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, MAN_INSTALL);
    if (st == TFW_OK) st = tfw_suit_string(&c, TFW_SUIT_MT_BSTR, TFW_SUIT_ENVELOPE_MAX, &s);
    if (st == TFW_OK) st = install(s, uri);
    if (st == TFW_OK) st = tfw_suit_done(&c);
    return st;
}

/* Nonzero when an id differs from the board's, read through TFW_PROGMEM. A
 * null `ram` reads as zero, so differs(0, id) == 0 is an id of all zero. */
static uint8_t differs(const uint8_t *ram, const uint8_t *pgm)
{
    uint8_t diff = 0u;
    uint8_t i;

    for (i = 0u; i < TFW_SUIT_ID_LEN; i++) {
        diff = (uint8_t)(diff | ((ram ? ram[i] : 0u) ^ tfw_pgm_u8(&pgm[i])));
    }
    return diff;
}

/* The kind the manifest claims, if it is one the caller expects: the board's
 * vendor, and the board's policy or script class (ADR D2). 0 for anything
 * else, and for a class the board leaves all zero. */
static uint8_t claimed(const tfw_suit_manifest_t *m, uint8_t expect)
{
    static const uint8_t vendor[TFW_SUIT_ID_LEN] TFW_PROGMEM = TFW_SUIT_VENDOR_ID;
    static const uint8_t policy[TFW_SUIT_ID_LEN] TFW_PROGMEM = TFW_SUIT_POLICY_CLASS_ID;
    static const uint8_t script[TFW_SUIT_ID_LEN] TFW_PROGMEM = TFW_SUIT_SCRIPT_CLASS_ID;
    uint8_t              kind = 0u;

    if (differs(m->vendor, vendor) != 0u) {
        return 0u;
    }
    if (differs(0, policy) != 0u && differs(m->class_id, policy) == 0u) {
        kind = TFW_SUIT_KIND_POLICY;
    } else if (differs(0, script) != 0u && differs(m->class_id, script) == 0u) {
        kind = TFW_SUIT_KIND_SCRIPT;
    }
    return (uint8_t)(kind & expect);
}

/* The kind the payload carries: a policy set is a map (D5), a script an
 * array (D6). Only the top-level type; the rest is the engine's (D7 step 7). */
static uint8_t carried(tfw_suit_cur_t body)
{
    uint8_t major;

    if (body.n == 0u) {
        return 0u;
    }
    major = (uint8_t)(body.p[0] >> 5);
    if (major == TFW_SUIT_MT_MAP) {
        return TFW_SUIT_KIND_POLICY;
    }
    return major == TFW_SUIT_MT_ARRAY ? TFW_SUIT_KIND_SCRIPT : 0u;
}

tfw_status_t tfw_suit_read_integrated(const uint8_t *env, uint16_t len,
                                      const uint8_t key[TFW_SUIT_KEY_LEN], uint8_t expect,
                                      tfw_suit_manifest_t *out, const uint8_t **payload,
                                      uint8_t *kind)
{
    tfw_suit_manifest_t m;
    tfw_suit_cur_t      c;
    tfw_suit_cur_t      auth;
    tfw_suit_cur_t      man;
    tfw_suit_cur_t      wrapped;
    tfw_suit_cur_t      name = { 0, 0u };
    tfw_suit_cur_t      body = { 0, 0u };
    tfw_suit_cur_t      uri;
    uint8_t             major;
    uint32_t            members = 0u;
    uint8_t             claim;
    uint8_t             h[TFW_SHA256_LEN];
    uint8_t             sig[TFW_SIG_LEN];
    tfw_status_t        st;

    if (len > TFW_SUIT_INTEGRATED_MAX) {
        return TFW_ERR_RANGE;
    }
    c.p = env;
    c.n = len;

    /* Two members, or three with the payload: a missing payload is the
     * payload's refusal (below), after the signature, not the structure's. */
    st = tfw_suit_expect(&c, TFW_SUIT_MT_TAG, TFW_SUIT_TAG_ENVELOPE);
    if (st == TFW_OK) st = tfw_suit_head(&c, &major, &members);
    if (st == TFW_OK && (major != TFW_SUIT_MT_MAP || members < 2u || members > 3u)) {
        st = TFW_ERR_UNSUPPORTED;
    }
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, TFW_SUIT_ENV_AUTH);
    if (st == TFW_OK) st = tfw_suit_string(&c, TFW_SUIT_MT_BSTR, TFW_SUIT_ENVELOPE_MAX, &auth);
    if (st == TFW_OK) st = tfw_suit_expect(&c, TFW_SUIT_MT_UINT, TFW_SUIT_ENV_MANIFEST);
    wrapped.p = c.p;
    if (st == TFW_OK) st = tfw_suit_string(&c, TFW_SUIT_MT_BSTR, TFW_SUIT_ENVELOPE_MAX, &man);
    wrapped.n = (uint16_t)(c.p - wrapped.p);
    if (st == TFW_OK && members == 3u) {
        st = tfw_suit_string(&c, TFW_SUIT_MT_TSTR, TFW_SUIT_URI_MAX, &name);
        if (st == TFW_OK) st = tfw_suit_string(&c, TFW_SUIT_MT_BSTR, len, &body);
    }
    if (st == TFW_OK) st = tfw_suit_done(&c);

    /* Authenticate under the caller's key before the manifest is read, and
     * before a byte of the payload is looked at. */
    if (st == TFW_OK) st = tfw_suit_auth(auth, &wrapped, h, sig);
    if (st == TFW_OK) st = tfw_crypto_verify_operator(h, sig, key);
    if (st == TFW_OK) st = manifest(man, &m, &uri);
    if (st != TFW_OK) {
        return st;
    }

    /* The vendor and class, before a byte of the payload (D7 step 4). */
    claim = claimed(&m, expect);
    if (claim == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }

    /* The payload: under the manifest's URI, its size, then its digest. */
    if (members != 3u || name.n != uri.n || memcmp(name.p, uri.p, uri.n) != 0
        || body.n != m.image_size) {
        return TFW_ERR_RANGE;
    }
    if (tfw_crypto_sha256(body.p, body.n, h) != TFW_OK
        || memcmp(h, m.image_digest, TFW_SUIT_DIGEST_LEN) != 0) {
        return TFW_ERR_CRYPTO;
    }
    /* A policy class carrying a script, or a script class a policy set. */
    if (carried(body) != claim) {
        return TFW_ERR_UNSUPPORTED;
    }
    *out     = m;
    *payload = body.p;
    *kind    = claim;
    return TFW_OK;
}
