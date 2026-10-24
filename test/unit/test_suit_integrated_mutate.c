/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutation tests for the integrated-payload reader (src/core/suit_integrated.c,
 * docs/hsm-self-defence/adr-response-policy-format.md D2, D7). Two halves:
 *
 * Every refusal path, one case each. A manifest is behind the signature, so a
 * fault in the manifest's walk is reached only by a manifest that is signed:
 * those cases are signed here, by the build's emitter (tools/suit-emit/) and
 * the throwaway tenant key whose private scalar testdata/suit_vectors.h names
 * (0x40..0x5F). Each case pins the exact status, so a check removed, moved or
 * loosened changes one -- a mutant of the reader fails a case here.
 *
 * Then the seeded mutation of test_suit_mutate.c over the integrated
 * vectors: bit flips, truncation, CBOR heads, length edges, appended bytes and
 * random buffers, read from a heap buffer of exactly the length passed. A read
 * that succeeds must be one of the two signed envelopes, of its kind.
 *
 * The seed and the counts are literals: a failure reproduces bit for bit.
 */
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "suit.h"
#include "sha256.h"
#include "uECC.h"
#include "emit.h"
#include "suit_vectors.h"

#define ITERATIONS 4000u
#define INPUT_CAP  (TFW_SUIT_INTEGRATED_MAX + 32u)
#define BOTH       ((uint8_t)(TFW_SUIT_KIND_POLICY | TFW_SUIT_KIND_SCRIPT))
#define UNSET      0xEEu

/* Offsets in SUIT_POLICY_ENVELOPE and SUIT_POLICY_MANIFEST (117 bytes, the
 * bstr wrapper included). */
#define ENV_MEMBERS    2u
#define ENV_MAN_KEY    121u
#define ENV_NAME       239u
#define ENV_BODY_HEAD  247u
#define MAN_WRAP_LEN   1u
#define MAN_INSTALL_LEN 100u

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static const uint8_t vendor[TFW_SUIT_ID_LEN]       = TFW_SUIT_VENDOR_ID;
static const uint8_t policy_class[TFW_SUIT_ID_LEN] = TFW_SUIT_POLICY_CLASS_ID;
static const uint8_t script_class[TFW_SUIT_ID_LEN] = TFW_SUIT_SCRIPT_CLASS_ID;

static uint32_t rng;
static uint8_t  priv[32];
static uint8_t  buf[INPUT_CAP];

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int ecdsa_rng(uint8_t *d, unsigned n)
{
    for (unsigned i = 0u; i < n; i++) {
        d[i] = (uint8_t)rnd();
    }
    return 1;
}

void setUp(void)
{
    uint8_t pub[64];

    rng = 0x5017C0DEu;
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    for (uint8_t i = 0u; i < sizeof priv; i++) {
        priv[i] = (uint8_t)(0x40u + i);
    }
    TEST_ASSERT_EQUAL_INT(1, uECC_compute_public_key(priv, pub, uECC_secp256r1()));
    TEST_ASSERT_EQUAL_MEMORY(SUIT_TENANT_PUB, pub, sizeof pub);
}

void tearDown(void) {}

/* `man` (bstr-wrapped) signed under the tenant key, in an envelope with
 * `body` under the key `name`. */
static uint16_t sign(uint8_t *out, const uint8_t *man, size_t mn, const char *name,
                     const uint8_t *body, size_t bn)
{
    uint8_t tbs[128];
    uint8_t h[32];
    uint8_t sig[64];
    size_t  tn = tfw_suit_emit_tbs(tbs, sizeof tbs, man, mn);
    size_t  n;

    TEST_ASSERT_NOT_EQUAL(0u, tn);
    tfw_sha256(tbs, (uint16_t)tn, h);
    uECC_set_rng(ecdsa_rng);
    TEST_ASSERT_EQUAL_INT(1, uECC_sign(priv, h, sizeof h, sig, uECC_secp256r1()));
    n = tfw_suit_emit_envelope_integrated(out, INPUT_CAP, man, mn, sig, name, body, bn);
    TEST_ASSERT_NOT_EQUAL(0u, n);
    return (uint16_t)n;
}

/* A manifest for `body` as the emitter writes one, then its envelope. */
static uint16_t build(uint8_t *out, const uint8_t *ven, const uint8_t *cls, const char *uri,
                      const char *name, const uint8_t *body, size_t bn, uint32_t size)
{
    uint8_t man[160];
    uint8_t dig[32];
    size_t  mn;

    tfw_sha256(body, (uint16_t)bn, dig);
    mn = tfw_suit_emit_manifest_integrated(man, sizeof man, dig, size, 3u, ven, cls, uri);
    TEST_ASSERT_NOT_EQUAL(0u, mn);
    return sign(out, man, mn, name, body, bn);
}

/* Read `n` bytes of `in` from a heap buffer of exactly that length. A
 * refusal leaves the manifest, the payload pointer and the kind alone; a read
 * returns the payload inside the envelope. */
static tfw_status_t rd(const uint8_t *in, uint16_t n, uint8_t expect, uint8_t *kind)
{
    tfw_suit_manifest_t m;
    tfw_suit_manifest_t sentinel;
    const uint8_t      *payload = 0;
    uint8_t            *heap    = malloc(n ? n : 1u);
    tfw_status_t        st;

    TEST_ASSERT_NOT_NULL(heap);
    memcpy(heap, in, n);
    memset(&m, 0xA5, sizeof m);
    memset(&sentinel, 0xA5, sizeof sentinel);
    *kind = UNSET;
    st    = tfw_suit_read_integrated(heap, n, SUIT_TENANT_PUB, expect, &m, &payload, kind);
    if (st != TFW_OK) {
        TEST_ASSERT_EQUAL_MEMORY(&sentinel, &m, sizeof m);
        TEST_ASSERT_NULL(payload);
        TEST_ASSERT_EQUAL_HEX8(UNSET, *kind);
    } else {
        TEST_ASSERT_TRUE(payload >= heap && payload + m.image_size <= heap + n);
    }
    free(heap);
    return st;
}

static void expect_status(tfw_status_t want, const uint8_t *in, uint16_t n)
{
    uint8_t kind;

    TEST_ASSERT_EQUAL(want, rd(in, n, BOTH, &kind));
}

/* SUIT_POLICY_ENVELOPE with one byte replaced. */
static void env_byte(tfw_status_t want, uint16_t at, uint8_t to, uint16_t n)
{
    memcpy(buf, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
    buf[at] = to;
    expect_status(want, buf, n);
}

/* SUIT_POLICY_MANIFEST with one byte replaced, signed. */
static void man_byte(tfw_status_t want, uint16_t at, uint8_t to)
{
    uint8_t man[sizeof SUIT_POLICY_MANIFEST];

    memcpy(man, SUIT_POLICY_MANIFEST, sizeof man);
    man[at] = to;
    expect_status(want, buf, sign(buf, man, sizeof man, "#policy", SUIT_POLICY_BODY,
                                  sizeof SUIT_POLICY_BODY));
}

/* The envelope's own structure, before the signature (D7 steps 1-3). */
static void test_the_envelope_refusals(void)
{
    uint8_t  kind;
    uint16_t n = 0u;

    /* Step 1: past the bound, and the bound itself, a signed envelope of
     * exactly TFW_SUIT_INTEGRATED_MAX bytes. */
    memset(buf, 0, sizeof buf);
    memcpy(buf, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
    expect_status(TFW_ERR_RANGE, buf, (uint16_t)(TFW_SUIT_INTEGRATED_MAX + 1u));
    {
        static uint8_t big[TFW_SUIT_INTEGRATED_MAX];
        uint32_t       size;

        memset(big, 0x00, sizeof big);
        big[0] = 0xA0u;
        for (size = 700u; size < sizeof big && n != TFW_SUIT_INTEGRATED_MAX; size++) {
            n = build(buf, vendor, policy_class, "#policy", "#policy", big, size, size);
        }
        TEST_ASSERT_EQUAL_UINT(TFW_SUIT_INTEGRATED_MAX, n);
        TEST_ASSERT_EQUAL(TFW_OK, rd(buf, n, BOTH, &kind));
        TEST_ASSERT_EQUAL_HEX8(TFW_SUIT_KIND_POLICY, kind);
    }

    env_byte(TFW_ERR_UNSUPPORTED, 1u, 0x6Cu, sizeof SUIT_POLICY_ENVELOPE);     /* tag 108 */
    env_byte(TFW_ERR_UNSUPPORTED, ENV_MEMBERS, 0x83u, sizeof SUIT_POLICY_ENVELOPE); /* array */
    env_byte(TFW_ERR_UNSUPPORTED, ENV_MEMBERS, 0xA1u, ENV_MAN_KEY);            /* {2} */
    env_byte(TFW_ERR_RANGE, ENV_MEMBERS, 0xA2u, ENV_NAME);                     /* detached */
    env_byte(TFW_ERR_UNSUPPORTED, ENV_MEMBERS, 0xA4u, ENV_NAME);               /* 4, 2 there */
    env_byte(TFW_ERR_UNSUPPORTED, 3u, 0x04u, sizeof SUIT_POLICY_ENVELOPE);     /* key 4 */
    env_byte(TFW_ERR_UNSUPPORTED, 4u, 0x78u, sizeof SUIT_POLICY_ENVELOPE);     /* auth tstr */
    env_byte(TFW_ERR_UNSUPPORTED, 6u, 0x83u, sizeof SUIT_POLICY_ENVELOPE);     /* auth [3] */
    env_byte(TFW_ERR_UNSUPPORTED, ENV_MAN_KEY, 0x04u, sizeof SUIT_POLICY_ENVELOPE);
    env_byte(TFW_ERR_UNSUPPORTED, ENV_MAN_KEY + 1u, 0x78u, sizeof SUIT_POLICY_ENVELOPE);
    env_byte(TFW_ERR_UNSUPPORTED, ENV_NAME, 0x47u, sizeof SUIT_POLICY_ENVELOPE);  /* name bstr */
    env_byte(TFW_ERR_UNSUPPORTED, ENV_BODY_HEAD, 0x76u, sizeof SUIT_POLICY_ENVELOPE);
    /* Trailing bytes. */
    memcpy(buf, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
    buf[sizeof SUIT_POLICY_ENVELOPE] = 0x00u;
    expect_status(TFW_ERR_UNSUPPORTED, buf, sizeof SUIT_POLICY_ENVELOPE + 1u);
    /* The manifest digest, then the signature. */
    env_byte(TFW_ERR_CRYPTO, 13u, (uint8_t)(SUIT_POLICY_ENVELOPE[13] ^ 0x01u),
             sizeof SUIT_POLICY_ENVELOPE);
    env_byte(TFW_ERR_CRYPTO, 57u, (uint8_t)(SUIT_POLICY_ENVELOPE[57] ^ 0x01u),
             sizeof SUIT_POLICY_ENVELOPE);
    env_byte(TFW_ERR_CRYPTO, 120u, (uint8_t)(SUIT_POLICY_ENVELOPE[120] ^ 0x80u),
             sizeof SUIT_POLICY_ENVELOPE);
}

/* The manifest's walk, behind a good signature (D7 step 2 for the manifest). */
static void test_the_manifest_refusals(void)
{
    uint8_t  man[sizeof SUIT_POLICY_MANIFEST + 1u];
    uint8_t  kind;
    uint16_t n;

    /* Unchanged and re-signed, it reads: the cases below differ by one byte. */
    n = sign(buf, SUIT_POLICY_MANIFEST, sizeof SUIT_POLICY_MANIFEST, "#policy",
             SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY);
    TEST_ASSERT_EQUAL(TFW_OK, rd(buf, n, BOTH, &kind));

    man_byte(TFW_ERR_UNSUPPORTED, 2u, 0xA5u);    /* a map of 5 */
    man_byte(TFW_ERR_UNSUPPORTED, 3u, 0x04u);    /* version key */
    man_byte(TFW_ERR_UNSUPPORTED, 4u, 0x02u);    /* version 2 */
    man_byte(TFW_ERR_UNSUPPORTED, 5u, 0x05u);    /* sequence key */
    man_byte(TFW_ERR_UNSUPPORTED, 6u, 0x40u);    /* sequence a bstr */
    man_byte(TFW_ERR_UNSUPPORTED, 7u, 0x04u);    /* common key */
    man_byte(TFW_ERR_UNSUPPORTED, 8u, 0x78u);    /* common a tstr */
    man_byte(TFW_ERR_UNSUPPORTED, 10u, 0xA3u);   /* common's map */
    man_byte(TFW_ERR_UNSUPPORTED, 99u, 0x15u);   /* install key */
    man_byte(TFW_ERR_UNSUPPORTED, 100u, 0x70u);  /* install a tstr */
    man_byte(TFW_ERR_UNSUPPORTED, 101u, 0x85u);  /* install [5] */
    man_byte(TFW_ERR_UNSUPPORTED, 102u, 0x13u);  /* not override-parameters */
    man_byte(TFW_ERR_UNSUPPORTED, 103u, 0xA2u);  /* parameters {2} */
    man_byte(TFW_ERR_UNSUPPORTED, 104u, 0x16u);  /* not the URI */
    man_byte(TFW_ERR_UNSUPPORTED, 105u, 0x47u);  /* URI a bstr */
    man_byte(TFW_ERR_UNSUPPORTED, 106u, 0x2Fu);  /* "/policy": not a fragment */
    man_byte(TFW_ERR_UNSUPPORTED, 113u, 0x16u);  /* not fetch */
    man_byte(TFW_ERR_UNSUPPORTED, 114u, 0x40u);  /* fetch's policy a bstr */
    man_byte(TFW_ERR_UNSUPPORTED, 115u, 0x04u);  /* not image match */
    man_byte(TFW_ERR_UNSUPPORTED, 116u, 0x40u);  /* image match's policy a bstr */

    /* A byte past install, inside its bstr; then past the manifest's map. */
    memcpy(man, SUIT_POLICY_MANIFEST, sizeof SUIT_POLICY_MANIFEST);
    man[sizeof SUIT_POLICY_MANIFEST] = 0x00u;
    man[MAN_WRAP_LEN]++;
    man[MAN_INSTALL_LEN]++;
    expect_status(TFW_ERR_UNSUPPORTED, buf, sign(buf, man, sizeof man, "#policy",
                                                 SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
    man[MAN_INSTALL_LEN]--;
    expect_status(TFW_ERR_UNSUPPORTED, buf, sign(buf, man, sizeof man, "#policy",
                                                 SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));

    /* A fragment of one character names nothing; of two it is a name. */
    expect_status(TFW_ERR_UNSUPPORTED, buf,
                  build(buf, vendor, policy_class, "#", "#", SUIT_POLICY_BODY,
                        sizeof SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
    expect_status(TFW_OK, buf, build(buf, vendor, policy_class, "#p", "#p", SUIT_POLICY_BODY,
                                     sizeof SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
}

/* The vendor and class (D7 step 4), each signed and differing by one byte at
 * either end of the id. */
static void test_the_binding_refusals(void)
{
    uint8_t id[TFW_SUIT_ID_LEN];
    uint8_t kind;
    uint8_t ends[2] = { 0u, TFW_SUIT_ID_LEN - 1u };

    for (uint8_t e = 0u; e < 2u; e++) {
        memcpy(id, vendor, sizeof id);
        id[ends[e]] ^= 0x01u;
        expect_status(TFW_ERR_UNSUPPORTED, buf,
                      build(buf, id, policy_class, "#policy", "#policy", SUIT_POLICY_BODY,
                            sizeof SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
        memcpy(id, policy_class, sizeof id);
        id[ends[e]] ^= 0x01u;
        expect_status(TFW_ERR_UNSUPPORTED, buf,
                      build(buf, vendor, id, "#policy", "#policy", SUIT_POLICY_BODY,
                            sizeof SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
        memcpy(id, script_class, sizeof id);
        id[ends[e]] ^= 0x01u;
        expect_status(TFW_ERR_UNSUPPORTED, buf,
                      build(buf, vendor, id, "#script", "#script", SUIT_SCRIPT_BODY,
                            sizeof SUIT_SCRIPT_BODY, sizeof SUIT_SCRIPT_BODY));
    }
    expect_status(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_CLASS_IMAGE,
                  sizeof SUIT_POLICY_ENVELOPE_CLASS_IMAGE);
    expect_status(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_CLASS_328P,
                  sizeof SUIT_POLICY_ENVELOPE_CLASS_328P);
    expect_status(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_CLASS_ZERO,
                  sizeof SUIT_POLICY_ENVELOPE_CLASS_ZERO);
    expect_status(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_VENDOR,
                  sizeof SUIT_POLICY_ENVELOPE_VENDOR);

    /* Which kinds the caller expects. */
    TEST_ASSERT_EQUAL(TFW_OK, rd(SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE,
                                 TFW_SUIT_KIND_POLICY, &kind));
    TEST_ASSERT_EQUAL_HEX8(TFW_SUIT_KIND_POLICY, kind);
    TEST_ASSERT_EQUAL(TFW_OK, rd(SUIT_SCRIPT_ENVELOPE, sizeof SUIT_SCRIPT_ENVELOPE,
                                 TFW_SUIT_KIND_SCRIPT, &kind));
    TEST_ASSERT_EQUAL_HEX8(TFW_SUIT_KIND_SCRIPT, kind);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, rd(SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE,
                                              TFW_SUIT_KIND_SCRIPT, &kind));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, rd(SUIT_SCRIPT_ENVELOPE, sizeof SUIT_SCRIPT_ENVELOPE,
                                              TFW_SUIT_KIND_POLICY, &kind));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, rd(SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE,
                                              (uint8_t)~BOTH, &kind));
}

/* The payload (D7 step 5) and the kind it carries (step 7). */
static void test_the_payload_refusals(void)
{
    static const uint8_t heads[] = { 0x00u, 0x20u, 0x40u, 0x60u, 0x80u, 0xA0u, 0xC0u, 0xE0u };
    uint8_t              body[sizeof SUIT_POLICY_BODY + 1u];
    uint8_t              kind;

    /* Under another name: one character different, and one longer. */
    expect_status(TFW_ERR_RANGE, buf,
                  build(buf, vendor, policy_class, "#policy", "#qolicy", SUIT_POLICY_BODY,
                        sizeof SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
    expect_status(TFW_ERR_RANGE, buf,
                  build(buf, vendor, policy_class, "#policy", "#policyX", SUIT_POLICY_BODY,
                        sizeof SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
    expect_status(TFW_ERR_RANGE, SUIT_POLICY_ENVELOPE_DETACHED,
                  sizeof SUIT_POLICY_ENVELOPE_DETACHED);
    /* A size one short of the manifest's, and one over. */
    expect_status(TFW_ERR_RANGE, buf,
                  build(buf, vendor, policy_class, "#policy", "#policy", SUIT_POLICY_BODY,
                        sizeof SUIT_POLICY_BODY - 1u, sizeof SUIT_POLICY_BODY));
    memcpy(body, SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY);
    body[sizeof SUIT_POLICY_BODY] = 0x00u;
    expect_status(TFW_ERR_RANGE, buf,
                  build(buf, vendor, policy_class, "#policy", "#policy", body, sizeof body,
                        sizeof SUIT_POLICY_BODY));
    /* The digest: its first byte and its last. */
    env_byte(TFW_ERR_CRYPTO, ENV_BODY_HEAD + 1u, 0xA4u, sizeof SUIT_POLICY_ENVELOPE);
    env_byte(TFW_ERR_CRYPTO, sizeof SUIT_POLICY_ENVELOPE - 1u, 0xF5u, sizeof SUIT_POLICY_ENVELOPE);

    /* Each major type under each class: a map is a policy set, an array a
     * script, and nothing else is either. */
    for (uint8_t i = 0u; i < sizeof heads; i++) {
        uint8_t      b[4] = { heads[i], 0x01u, 0x02u, 0x03u };
        tfw_status_t want;
        uint16_t     n;

        want = heads[i] == 0xA0u ? TFW_OK : TFW_ERR_UNSUPPORTED;
        n    = build(buf, vendor, policy_class, "#policy", "#policy", b, sizeof b, sizeof b);
        TEST_ASSERT_EQUAL(want, rd(buf, n, BOTH, &kind));
        want = heads[i] == 0x80u ? TFW_OK : TFW_ERR_UNSUPPORTED;
        n    = build(buf, vendor, script_class, "#script", "#script", b, sizeof b, sizeof b);
        TEST_ASSERT_EQUAL(want, rd(buf, n, BOTH, &kind));
    }
    expect_status(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_CARRIES_SCRIPT,
                  sizeof SUIT_POLICY_ENVELOPE_CARRIES_SCRIPT);
    expect_status(TFW_ERR_UNSUPPORTED, SUIT_SCRIPT_ENVELOPE_CARRIES_POLICY,
                  sizeof SUIT_SCRIPT_ENVELOPE_CARRIES_POLICY);
    /* Size 0, with a map head just past the envelope for a reader that
     * looked past its payload to find. */
    memcpy(buf, SUIT_POLICY_ENVELOPE_EMPTY, sizeof SUIT_POLICY_ENVELOPE_EMPTY);
    buf[sizeof SUIT_POLICY_ENVELOPE_EMPTY] = 0xA0u;
    {
        tfw_suit_manifest_t m;
        const uint8_t      *payload = 0;

        kind = UNSET;
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
                          tfw_suit_read_integrated(buf, sizeof SUIT_POLICY_ENVELOPE_EMPTY,
                                                   SUIT_TENANT_PUB, BOTH, &m, &payload, &kind));
        TEST_ASSERT_EQUAL_HEX8(UNSET, kind);
    }
}

/* tfw_suit_integrated_digest: SUIT_POLICY_ENVELOPE with byte `at` set to
 * `to`, read over its first n bytes. A refusal leaves the digest as it was. */
static void digest_refused(tfw_status_t want, uint16_t at, uint8_t to, uint16_t n)
{
    uint8_t d[TFW_SUIT_DIGEST_LEN];
    uint8_t before[TFW_SUIT_DIGEST_LEN];

    memcpy(buf, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
    buf[at] = to;
    memset(d, 0xA5, sizeof d);
    memset(before, 0xA5, sizeof before);
    TEST_ASSERT_EQUAL(want, tfw_suit_integrated_digest(buf, n, d));
    TEST_ASSERT_EQUAL_MEMORY(before, d, sizeof d);
}

static void test_the_integrated_digest_is_the_hash_of_the_wrapped_manifest(void)
{
    uint8_t d[TFW_SUIT_DIGEST_LEN];
    uint8_t want[TFW_SHA256_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(&SUIT_POLICY_ENVELOPE[ENV_MAN_KEY + 1u],
                                                (uint16_t)(ENV_NAME - ENV_MAN_KEY - 1u), want));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_integrated_digest(SUIT_POLICY_ENVELOPE,
                                                         sizeof SUIT_POLICY_ENVELOPE, d));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, d, sizeof d);
}

static void test_an_integrated_digest_under_another_tag_is_refused(void)
{
    digest_refused(TFW_ERR_UNSUPPORTED, 1u, 0x6Cu, sizeof SUIT_POLICY_ENVELOPE);   /* tag 108 */
}

static void test_an_integrated_digest_with_another_first_key_is_refused(void)
{
    digest_refused(TFW_ERR_UNSUPPORTED, 3u, 0x04u, sizeof SUIT_POLICY_ENVELOPE);
}

static void test_an_integrated_digest_with_an_authentication_text_string_is_refused(void)
{
    digest_refused(TFW_ERR_UNSUPPORTED, 4u, 0x78u, sizeof SUIT_POLICY_ENVELOPE);
}

static void test_an_integrated_digest_with_another_second_key_is_refused(void)
{
    digest_refused(TFW_ERR_UNSUPPORTED, ENV_MAN_KEY, 0x04u, sizeof SUIT_POLICY_ENVELOPE);
}

static void test_an_integrated_digest_with_a_manifest_text_string_is_refused(void)
{
    digest_refused(TFW_ERR_UNSUPPORTED, ENV_MAN_KEY + 1u, 0x78u, sizeof SUIT_POLICY_ENVELOPE);
}

/* Every length that ends before the wrapped manifest does: the tag, the
 * map's head, either key or either string cut short. */
static void test_an_integrated_digest_of_a_truncated_envelope_is_refused(void)
{
    uint16_t n;

    for (n = 0u; n < ENV_NAME; n++) {
        digest_refused(TFW_ERR_RANGE, 0u, SUIT_POLICY_ENVELOPE[0], n);
    }
}

typedef struct {
    const uint8_t *bytes;
    uint16_t       len;
} seed_t;

static const seed_t seeds[] = {
    { SUIT_POLICY_ENVELOPE,                (uint16_t)sizeof SUIT_POLICY_ENVELOPE },
    { SUIT_SCRIPT_ENVELOPE,                (uint16_t)sizeof SUIT_SCRIPT_ENVELOPE },
    { SUIT_POLICY_ENVELOPE_DETACHED,       (uint16_t)sizeof SUIT_POLICY_ENVELOPE_DETACHED },
    { SUIT_POLICY_ENVELOPE_CLASS_328P,     (uint16_t)sizeof SUIT_POLICY_ENVELOPE_CLASS_328P },
    { SUIT_POLICY_ENVELOPE_CARRIES_SCRIPT, (uint16_t)sizeof SUIT_POLICY_ENVELOPE_CARRIES_SCRIPT },
    { SUIT_SCRIPT_ENVELOPE_CARRIES_POLICY, (uint16_t)sizeof SUIT_SCRIPT_ENVELOPE_CARRIES_POLICY },
};
#define SEED_COUNT ((uint32_t)(sizeof seeds / sizeof seeds[0]))

static const uint8_t heads[] = {
    0x58u, 0x59u, 0x5Au, 0x5Bu, 0x78u, 0x79u, 0x98u, 0x99u, 0x9Au, 0x9Bu, 0xB8u, 0xB9u,
    0xBAu, 0xBBu, 0x5Fu, 0x7Fu, 0x9Fu, 0xBFu, 0xFFu, 0xD8u, 0xD2u,
};
static const uint8_t args[] = { 0x00u, 0x01u, 0x7Fu, 0x80u, 0xFEu, 0xFFu };

static uint16_t make_input(uint8_t *b)
{
    const seed_t *s   = &seeds[rnd() % SEED_COUNT];
    uint16_t      n   = s->len;
    uint32_t      ops = 1u + rnd() % 3u;

    if (rnd() % 8u == 0u) {
        n = (uint16_t)(1u + rnd() % INPUT_CAP);
        for (uint16_t i = 0; i < n; i++) {
            b[i] = (uint8_t)rnd();
        }
        return n;
    }
    memcpy(b, s->bytes, n);
    while (ops-- > 0u) {
        uint16_t at = (uint16_t)(rnd() % n);

        switch (rnd() % 7u) {
        case 0:
            for (uint32_t k = 1u + rnd() % 8u; k > 0u; k--) {
                b[rnd() % n] ^= (uint8_t)(1u << (rnd() % 8u));
            }
            break;
        case 1:
            n = (uint16_t)(1u + rnd() % n);
            break;
        case 2:
            b[at] = heads[rnd() % sizeof heads];
            break;
        case 3:
            b[at] = args[rnd() % sizeof args];
            break;
        case 4:
            for (uint32_t k = 1u + rnd() % 32u; k > 0u && n < INPUT_CAP; k--) {
                b[n++] = (uint8_t)rnd();
            }
            break;
        case 5:
            b[at] = (uint8_t)((b[at] & 0xE0u) | ((rnd() % 3u == 0u) ? 0u : (rnd() % 2u) ? 23u : 24u));
            break;
        default:
            b[at] = (uint8_t)rnd();
            break;
        }
    }
    return n;
}

static void test_mutated_envelopes_read_inside_their_contract(void)
{
    static uint8_t scratch[INPUT_CAP];
    unsigned long  ok = 0u, refused = 0u, crypto = 0u;

    for (uint32_t it = 0; it < ITERATIONS; it++) {
        uint16_t            n  = make_input(scratch);
        uint8_t            *in = malloc(n);
        tfw_suit_manifest_t m, sentinel;
        const uint8_t      *payload = 0;
        uint8_t             kind    = UNSET;
        tfw_status_t        st;

        TEST_ASSERT_NOT_NULL(in);
        memcpy(in, scratch, n);
        memset(&m, 0xA5, sizeof m);
        memcpy(&sentinel, &m, sizeof sentinel);
        st = tfw_suit_read_integrated(in, n, SUIT_TENANT_PUB, BOTH, &m, &payload, &kind);
        TEST_ASSERT_TRUE(st == TFW_OK || st == TFW_ERR_RANGE ||
                         st == TFW_ERR_UNSUPPORTED || st == TFW_ERR_CRYPTO);
        if (st != TFW_OK) {
            TEST_ASSERT_EQUAL_MEMORY(&sentinel, &m, sizeof m);
            TEST_ASSERT_NULL(payload);
            TEST_ASSERT_EQUAL_HEX8(UNSET, kind);
        }
        if (st == TFW_OK) {
            ok++;
            /* One of the two signed envelopes, of its own kind. */
            if (kind == TFW_SUIT_KIND_POLICY) {
                TEST_ASSERT_EQUAL_UINT32(sizeof SUIT_POLICY_BODY, m.image_size);
                TEST_ASSERT_EQUAL_MEMORY(policy_class, m.class_id, sizeof m.class_id);
                TEST_ASSERT_EQUAL_MEMORY(SUIT_POLICY_BODY, payload, sizeof SUIT_POLICY_BODY);
            } else {
                TEST_ASSERT_EQUAL_HEX8(TFW_SUIT_KIND_SCRIPT, kind);
                TEST_ASSERT_EQUAL_UINT32(sizeof SUIT_SCRIPT_BODY, m.image_size);
                TEST_ASSERT_EQUAL_MEMORY(script_class, m.class_id, sizeof m.class_id);
                TEST_ASSERT_EQUAL_MEMORY(SUIT_SCRIPT_BODY, payload, sizeof SUIT_SCRIPT_BODY);
            }
        } else if (st == TFW_ERR_CRYPTO) {
            crypto++;
        } else {
            refused++;
        }
        free(in);
    }

    /* Proof the loop ran: some inputs were refused by the walk, and some got
     * past it to the digest and signature. */
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, crypto);
    (void)ok;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_envelope_refusals);
    RUN_TEST(test_the_manifest_refusals);
    RUN_TEST(test_the_binding_refusals);
    RUN_TEST(test_the_payload_refusals);
    RUN_TEST(test_the_integrated_digest_is_the_hash_of_the_wrapped_manifest);
    RUN_TEST(test_an_integrated_digest_under_another_tag_is_refused);
    RUN_TEST(test_an_integrated_digest_with_another_first_key_is_refused);
    RUN_TEST(test_an_integrated_digest_with_an_authentication_text_string_is_refused);
    RUN_TEST(test_an_integrated_digest_with_another_second_key_is_refused);
    RUN_TEST(test_an_integrated_digest_with_a_manifest_text_string_is_refused);
    RUN_TEST(test_an_integrated_digest_of_a_truncated_envelope_is_refused);
    RUN_TEST(test_mutated_envelopes_read_inside_their_contract);
    return UNITY_END();
}
