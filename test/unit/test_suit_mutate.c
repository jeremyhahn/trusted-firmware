/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A seeded mutation test for the SUIT envelope reader (issue #78). The
 * envelope is staged with an OTA image, over TCP or serial: every byte of it
 * is the sender's until the signature says otherwise, and the CBOR walk that
 * finds the signature runs before it.
 *
 * The inputs are the literal envelopes, mutated -- bit flips, truncation, a
 * byte replaced by a CBOR head that claims a 1, 2, 4 or 8-byte length, a
 * length argument set to 0, its maximum and past it, appended bytes -- plus
 * pure random buffers. Each is read from a heap buffer of exactly the length
 * passed, so under `make test-asan` a read past the envelope is a fault, not
 * a pass.
 *
 * A read that succeeds must have read one of the two signed manifests: the
 * signature covers the manifest, so any success that differs from both is a
 * reader that judged one span and parsed another.
 *
 * The seed and the counts are literals: a failure reproduces bit for bit.
 */
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "suit.h"
#include "suit_cbor.h"
#include "suit_vectors.h"

#define ITERATIONS 4000u
#define INPUT_CAP  (TFW_SUIT_ENVELOPE_MAX + 32u)

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static uint32_t            rng;
static tfw_suit_manifest_t signed_a, signed_b;

void setUp(void)
{
    rng = 0x5017C0DEu;
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, SUIT_PUB);
}

void tearDown(void) {}

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

typedef struct {
    const uint8_t *bytes;
    uint16_t       len;
} seed_t;

static const seed_t seeds[] = {
    { SUIT_ENVELOPE,            (uint16_t)sizeof SUIT_ENVELOPE },
    { SUIT_ENVELOPE_CLASS_328P, (uint16_t)sizeof SUIT_ENVELOPE_CLASS_328P },
    { SUIT_ENVELOPE_UNKNOWN,    (uint16_t)sizeof SUIT_ENVELOPE_UNKNOWN },
    { SUIT_MANIFEST,            (uint16_t)sizeof SUIT_MANIFEST },
};
#define SEED_COUNT ((uint32_t)(sizeof seeds / sizeof seeds[0]))

/* CBOR heads whose argument follows in 1, 2, 4 or 8 bytes, for a byte string,
 * an array and a map, and the argument values at the edges. */
static const uint8_t heads[] = {
    0x58u, 0x59u, 0x5Au, 0x5Bu, 0x98u, 0x99u, 0x9Au, 0x9Bu, 0xB8u, 0xB9u, 0xBAu, 0xBBu,
    0x5Fu, 0x9Fu, 0xBFu, 0xFFu, 0xD8u, 0xD2u,
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
            /* A length argument: the byte after a head, set to an edge. */
            b[at] = args[rnd() % sizeof args];
            break;
        case 4:
            for (uint32_t k = 1u + rnd() % 32u; k > 0u && n < INPUT_CAP; k--) {
                b[n++] = (uint8_t)rnd();
            }
            break;
        case 5:
            /* A short head's inline length: 0, 23 (the largest), 24 (the next
             * byte holds it), keeping the major type. */
            b[at] = (uint8_t)((b[at] & 0xE0u) | ((rnd() % 3u == 0u) ? 0u : (rnd() % 2u) ? 23u : 24u));
            break;
        default:
            b[at] = (uint8_t)rnd();
            break;
        }
    }
    return n;
}

static int same(const tfw_suit_manifest_t *a, const tfw_suit_manifest_t *b)
{
    return a->seq == b->seq && a->image_size == b->image_size &&
           memcmp(a->vendor, b->vendor, sizeof a->vendor) == 0 &&
           memcmp(a->class_id, b->class_id, sizeof a->class_id) == 0 &&
           memcmp(a->image_digest, b->image_digest, sizeof a->image_digest) == 0;
}

static void test_mutated_envelopes_read_inside_their_contract(void)
{
    static uint8_t scratch[INPUT_CAP];
    unsigned long  ok = 0u, refused = 0u, crypto = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read(SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, &signed_a));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read(SUIT_ENVELOPE_CLASS_328P,
                                            sizeof SUIT_ENVELOPE_CLASS_328P, &signed_b));

    for (uint32_t it = 0; it < ITERATIONS; it++) {
        uint16_t            n  = make_input(scratch);
        uint8_t            *in = malloc(n);
        tfw_suit_manifest_t m, sentinel;
        tfw_status_t        st;

        TEST_ASSERT_NOT_NULL(in);
        memcpy(in, scratch, n);
        memset(&m, 0xA5, sizeof m);
        memcpy(&sentinel, &m, sizeof sentinel);
        st = tfw_suit_read(in, n, &m);
        TEST_ASSERT_TRUE(st == TFW_OK || st == TFW_ERR_RANGE ||
                         st == TFW_ERR_UNSUPPORTED || st == TFW_ERR_CRYPTO);
        /* Every refusal, the walk's and the signature's, leaves *out as it was. */
        if (st != TFW_OK) {
            TEST_ASSERT_EQUAL_MEMORY(&sentinel, &m, sizeof m);
        }
        if (st == TFW_OK) {
            ok++;
            TEST_ASSERT_TRUE(n <= TFW_SUIT_ENVELOPE_MAX);
            TEST_ASSERT_TRUE(same(&m, &signed_a) || same(&m, &signed_b));
        } else if (st == TFW_ERR_CRYPTO) {
            crypto++;
        } else {
            refused++;
        }
        free(in);
    }

    /* Proof the loop ran: some inputs were refused by the walk, and some got
     * past it to the digest and signature, the reader's last checks. */
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, crypto);
    (void)ok;
}

/* ---- each refusal on its own branch, with nothing written past it ----
 *
 * The envelope's outer refusals come before the signature is checked, so a
 * one-byte change to SUIT_ENVELOPE reaches each of them. The authentication
 * wrapper's are reached through tfw_suit_auth directly, on a wrapper built
 * here: its structure is checked before any digest is compared, so no
 * signature is needed to reach a refusal. */

#define AUTH_CAP     160u
#define KID_LEN_OK   8u    /* inside KID_MAX, 32 */
#define UNTOUCHED    0xEEu

static const uint8_t wrapped_manifest[3] = { 0x43u, 'a', 'b' };
static uint8_t       auth_buf[AUTH_CAP];
static uint8_t       auth_tbs[TFW_SHA256_LEN];
static uint8_t       auth_sig[TFW_SIG_LEN];

/* [digest wrapper, COSE_Sign1] with an optional kid and an optional trailing
 * byte in each of the digest wrapper, the COSE_Sign1 and the wrapper itself. */
static uint16_t build_auth(uint8_t kid_len, uint8_t dg_extra, uint8_t cose_extra,
                           uint8_t auth_extra)
{
    static const uint8_t sign1_head[7] = { 0xD2u, 0x84u, 0x43u, 0xA1u, 0x01u, 0x26u, 0xA0u };
    uint8_t  cose[AUTH_CAP];
    uint16_t c = 0u;
    uint16_t n = 0u;

    memcpy(cose, sign1_head, sizeof sign1_head);
    c = sizeof sign1_head;
    if (kid_len != 0u) {
        cose[c - 1u] = 0xA1u;
        cose[c++] = 0x04u;
        cose[c++] = 0x58u;
        cose[c++] = kid_len;
        memset(&cose[c], 0x4Bu, kid_len);
        c = (uint16_t)(c + kid_len);
    }
    cose[c++] = 0xF6u;
    cose[c++] = 0x58u;
    cose[c++] = (uint8_t)TFW_SIG_LEN;
    memset(&cose[c], 0x5Au, TFW_SIG_LEN);
    c = (uint16_t)(c + TFW_SIG_LEN);
    if (cose_extra) { cose[c++] = 0x00u; }

    auth_buf[n++] = 0x82u;
    auth_buf[n++] = 0x58u;
    auth_buf[n++] = (uint8_t)(36u + dg_extra);
    auth_buf[n++] = 0x82u;
    auth_buf[n++] = 0x2Fu;
    auth_buf[n++] = 0x58u;
    auth_buf[n++] = 0x20u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(wrapped_manifest, sizeof wrapped_manifest,
                                                &auth_buf[n]));
    n = (uint16_t)(n + 32u);
    if (dg_extra) { auth_buf[n++] = 0x00u; }
    auth_buf[n++] = 0x58u;
    auth_buf[n++] = (uint8_t)c;
    memcpy(&auth_buf[n], cose, c);
    n = (uint16_t)(n + c);
    if (auth_extra) { auth_buf[n++] = 0x00u; }
    return n;
}

static tfw_status_t run_auth(uint16_t n)
{
    tfw_suit_cur_t auth    = { auth_buf, n };
    tfw_suit_cur_t wrapped = { wrapped_manifest, sizeof wrapped_manifest };

    memset(auth_tbs, UNTOUCHED, sizeof auth_tbs);
    memset(auth_sig, UNTOUCHED, sizeof auth_sig);
    return tfw_suit_auth(auth, &wrapped, auth_tbs, auth_sig);
}

static void assert_untouched(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, p[i]);
    }
}

/* One byte of the built wrapper changed: refused with want, before the
 * signature or the to-be-signed hash is written. */
static void assert_auth_refused(uint16_t at, uint8_t to, tfw_status_t want)
{
    uint16_t n = build_auth(0u, 0u, 0u, 0u);

    auth_buf[at] = to;
    TEST_ASSERT_EQUAL(want, run_auth(n));
    assert_untouched(auth_tbs, sizeof auth_tbs);
    assert_untouched(auth_sig, sizeof auth_sig);
}

/* One byte of SUIT_ENVELOPE changed: refused with want before the signature
 * is checked, and the caller's manifest is not written. */
static void assert_envelope_refused(uint16_t at, uint8_t to, tfw_status_t want)
{
    static uint8_t      env[sizeof SUIT_ENVELOPE];
    tfw_suit_manifest_t m;

    memcpy(env, SUIT_ENVELOPE, sizeof env);
    TEST_ASSERT_NOT_EQUAL_HEX8(to, env[at]);
    env[at] = to;
    memset(&m, UNTOUCHED, sizeof m);
    TEST_ASSERT_EQUAL(want, tfw_suit_read(env, sizeof env, &m));
    assert_untouched((const uint8_t *)&m, sizeof m);
}

static void test_the_built_auth_wrapper_reads(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run_auth(build_auth(0u, 0u, 0u, 0u)));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, auth_sig[0]);
    TEST_ASSERT_EQUAL(TFW_OK, run_auth(build_auth(KID_LEN_OK, 0u, 0u, 0u)));
}

static void test_a_head_on_an_empty_cursor_is_refused_and_the_cursor_stays(void)
{
    static const uint8_t b[1] = { 0x00u };
    tfw_suit_cur_t c = { b, 0u };
    uint8_t        major = UNTOUCHED;
    uint32_t       val   = 0xEEEEEEEEu;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_suit_head(&c, &major, &val));
    TEST_ASSERT_EQUAL_PTR(b, c.p);
    TEST_ASSERT_EQUAL_UINT16(0u, c.n);
    TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, major);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, val);
}

static void test_a_head_whose_argument_is_cut_short_is_refused_and_no_value_is_written(void)
{
    static const uint8_t b[2] = { 0x19u, 0x01u };   /* a two-byte argument, one byte sent */
    tfw_suit_cur_t c = { b, sizeof b };
    uint8_t        major;
    uint32_t       val = 0xEEEEEEEEu;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_suit_head(&c, &major, &val));
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, val);
    TEST_ASSERT_EQUAL_PTR(&b[1], c.p);   /* the initial byte, and no more */
}

static void test_an_envelope_without_the_suit_tag_is_refused(void)
{
    assert_envelope_refused(1u, 0x6Cu, TFW_ERR_UNSUPPORTED);
}

static void test_an_envelope_that_is_not_a_two_member_map_is_refused(void)
{
    assert_envelope_refused(2u, 0xA3u, TFW_ERR_UNSUPPORTED);
}

static void test_an_envelope_whose_first_key_is_not_authentication_is_refused(void)
{
    assert_envelope_refused(3u, 0x03u, TFW_ERR_UNSUPPORTED);
}

static void test_an_envelope_whose_authentication_is_not_a_bstr_is_refused(void)
{
    assert_envelope_refused(4u, 0x78u, TFW_ERR_UNSUPPORTED);
}

static void test_an_envelope_whose_second_key_is_not_the_manifest_is_refused(void)
{
    assert_envelope_refused(121u, 0x04u, TFW_ERR_UNSUPPORTED);
}

static void test_an_envelope_whose_manifest_is_not_a_bstr_is_refused(void)
{
    assert_envelope_refused(122u, 0x78u, TFW_ERR_UNSUPPORTED);
}

static void test_an_auth_wrapper_that_is_not_a_two_member_array_is_refused(void)
{
    assert_auth_refused(0u, 0x83u, TFW_ERR_UNSUPPORTED);
}

static void test_a_digest_wrapper_longer_than_its_bound_is_refused(void)
{
    assert_auth_refused(2u, 41u, TFW_ERR_RANGE);
}

static void test_a_digest_that_is_not_a_two_member_array_is_refused(void)
{
    assert_auth_refused(3u, 0x83u, TFW_ERR_UNSUPPORTED);
}

static void test_a_digest_algorithm_other_than_sha256_is_refused(void)
{
    assert_auth_refused(4u, 0x26u, TFW_ERR_UNSUPPORTED);   /* -7 */
}

static void test_a_digest_shorter_than_sha256_is_refused(void)
{
    assert_auth_refused(6u, 0x1Fu, TFW_ERR_UNSUPPORTED);
}

static void test_a_digest_longer_than_sha256_is_refused(void)
{
    assert_auth_refused(6u, 0x21u, TFW_ERR_RANGE);
}

static void test_a_trailing_byte_in_the_digest_wrapper_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run_auth(build_auth(0u, 1u, 0u, 0u)));
    assert_untouched(auth_tbs, sizeof auth_tbs);
    assert_untouched(auth_sig, sizeof auth_sig);
}

static void test_a_cose_member_that_is_not_a_bstr_is_refused(void)
{
    assert_auth_refused(39u, 0x78u, TFW_ERR_UNSUPPORTED);
}

static void test_a_cose_tag_other_than_sign1_is_refused(void)
{
    assert_auth_refused(41u, 0xD1u, TFW_ERR_UNSUPPORTED);
}

static void test_a_sign1_that_is_not_a_four_member_array_is_refused(void)
{
    assert_auth_refused(42u, 0x83u, TFW_ERR_UNSUPPORTED);
}

static void test_a_protected_header_longer_than_es256_is_refused(void)
{
    assert_auth_refused(43u, 0x44u, TFW_ERR_RANGE);
}

static void test_a_protected_header_shorter_than_es256_is_refused(void)
{
    assert_auth_refused(43u, 0x42u, TFW_ERR_UNSUPPORTED);
}

static void test_a_protected_header_naming_another_algorithm_is_refused(void)
{
    assert_auth_refused(46u, 0x27u, TFW_ERR_UNSUPPORTED);   /* -8, EdDSA */
}

static void test_an_unprotected_header_that_is_not_a_map_is_refused(void)
{
    assert_auth_refused(47u, 0x80u, TFW_ERR_UNSUPPORTED);
}

static void test_an_unprotected_header_of_two_members_is_refused(void)
{
    assert_auth_refused(47u, 0xA2u, TFW_ERR_UNSUPPORTED);
}

static void test_an_unprotected_member_other_than_kid_is_refused(void)
{
    uint16_t n = build_auth(KID_LEN_OK, 0u, 0u, 0u);

    auth_buf[48] = 0x05u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run_auth(n));
    assert_untouched(auth_tbs, sizeof auth_tbs);
    assert_untouched(auth_sig, sizeof auth_sig);
}

static void test_a_kid_longer_than_its_bound_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run_auth(build_auth(33u, 0u, 0u, 0u)));
    assert_untouched(auth_tbs, sizeof auth_tbs);
    assert_untouched(auth_sig, sizeof auth_sig);
}

static void test_a_sign1_carrying_its_payload_is_refused(void)
{
    assert_auth_refused(48u, 0xF5u, TFW_ERR_UNSUPPORTED);
}

static void test_a_signature_shorter_than_p256_is_refused(void)
{
    assert_auth_refused(50u, (uint8_t)(TFW_SIG_LEN - 1u), TFW_ERR_UNSUPPORTED);
}

static void test_a_signature_longer_than_p256_is_refused(void)
{
    assert_auth_refused(50u, (uint8_t)(TFW_SIG_LEN + 1u), TFW_ERR_RANGE);
}

/* The two refusals after the signature is copied out: the signature buffer
 * is the caller's scratch, so only the to-be-signed hash is held to it. */
static void test_a_trailing_byte_in_the_sign1_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run_auth(build_auth(0u, 0u, 1u, 0u)));
    assert_untouched(auth_tbs, sizeof auth_tbs);
}

static void test_a_trailing_member_in_the_auth_wrapper_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run_auth(build_auth(0u, 0u, 0u, 1u)));
    assert_untouched(auth_tbs, sizeof auth_tbs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_envelopes_read_inside_their_contract);
    RUN_TEST(test_the_built_auth_wrapper_reads);
    RUN_TEST(test_a_head_on_an_empty_cursor_is_refused_and_the_cursor_stays);
    RUN_TEST(test_a_head_whose_argument_is_cut_short_is_refused_and_no_value_is_written);
    RUN_TEST(test_an_envelope_without_the_suit_tag_is_refused);
    RUN_TEST(test_an_envelope_that_is_not_a_two_member_map_is_refused);
    RUN_TEST(test_an_envelope_whose_first_key_is_not_authentication_is_refused);
    RUN_TEST(test_an_envelope_whose_authentication_is_not_a_bstr_is_refused);
    RUN_TEST(test_an_envelope_whose_second_key_is_not_the_manifest_is_refused);
    RUN_TEST(test_an_envelope_whose_manifest_is_not_a_bstr_is_refused);
    RUN_TEST(test_an_auth_wrapper_that_is_not_a_two_member_array_is_refused);
    RUN_TEST(test_a_digest_wrapper_longer_than_its_bound_is_refused);
    RUN_TEST(test_a_digest_that_is_not_a_two_member_array_is_refused);
    RUN_TEST(test_a_digest_algorithm_other_than_sha256_is_refused);
    RUN_TEST(test_a_digest_shorter_than_sha256_is_refused);
    RUN_TEST(test_a_digest_longer_than_sha256_is_refused);
    RUN_TEST(test_a_trailing_byte_in_the_digest_wrapper_is_refused);
    RUN_TEST(test_a_cose_member_that_is_not_a_bstr_is_refused);
    RUN_TEST(test_a_cose_tag_other_than_sign1_is_refused);
    RUN_TEST(test_a_sign1_that_is_not_a_four_member_array_is_refused);
    RUN_TEST(test_a_protected_header_longer_than_es256_is_refused);
    RUN_TEST(test_a_protected_header_shorter_than_es256_is_refused);
    RUN_TEST(test_a_protected_header_naming_another_algorithm_is_refused);
    RUN_TEST(test_an_unprotected_header_that_is_not_a_map_is_refused);
    RUN_TEST(test_an_unprotected_header_of_two_members_is_refused);
    RUN_TEST(test_an_unprotected_member_other_than_kid_is_refused);
    RUN_TEST(test_a_kid_longer_than_its_bound_is_refused);
    RUN_TEST(test_a_sign1_carrying_its_payload_is_refused);
    RUN_TEST(test_a_signature_shorter_than_p256_is_refused);
    RUN_TEST(test_a_signature_longer_than_p256_is_refused);
    RUN_TEST(test_a_trailing_byte_in_the_sign1_is_refused);
    RUN_TEST(test_a_trailing_member_in_the_auth_wrapper_is_refused);
    return UNITY_END();
}
