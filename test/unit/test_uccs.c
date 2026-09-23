/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Unprotected CWT Claims Set.
 *
 * The encoder has one job and the vector is how it is checked: these bytes went
 * through a real CBOR decoder before they were frozen, so a pass here means the
 * firmware emits something a verifier can actually read -- not merely something
 * this codebase agrees with itself about.
 *
 * The refusals matter as much. A claims set that was truncated, or built around
 * a nonce of the wrong width, is evidence that says the verifier challenged
 * with bytes it never sent.
 */

#include <string.h>
#include "unity.h"
#include "uccs.h"
#include "uccs_vectors.h"
#include "ag/crypto.h"
#include "ag/protocol.h"

static const uint8_t NONCE[]  = AGV_UCCS_NONCE;
static const uint8_t UEID[]   = AGV_UCCS_UEID;
static const uint8_t FWHASH[] = AGV_UCCS_FWHASH;
static const uint8_t WANT[]   = AGV_UCCS;

static uint8_t     buf[AG_UCCS_LEN + 8u];
static ag_writer_t w;

void setUp(void)
{
    memset(buf, 0xCC, sizeof buf);
    ag_writer_init(&w, buf, AG_UCCS_LEN);
}

void tearDown(void) {}

static ag_status_t encode(void)
{
    return ag_uccs_encode(&w, NONCE, AG_ALIAS_NONCE_LEN, UEID, FWHASH);
}

/* ------------------------------------------------------------ the vector */

/*
 * THE CONTRACT. Every byte, in order. This is the value that was decoded with
 * an independent CBOR implementation and found to carry claim keys 10, 256 and
 * 273 and nothing else, with no trailing bytes.
 */
static void test_the_claims_set_is_the_bytes_the_vector_pins(void)
{
    TEST_ASSERT_EQUAL(AG_OK, encode());
    TEST_ASSERT_EQUAL_UINT8(AGV_UCCS_LEN, ag_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, AGV_UCCS_LEN);
}

/* The constant the certificate template is built around. If the encoder and the
 * template disagree about this number the certificate's DER lengths describe a
 * different document than the one that follows them. */
static void test_the_declared_length_is_the_length_emitted(void)
{
    TEST_ASSERT_EQUAL_UINT8(AG_UCCS_LEN, AGV_UCCS_LEN);
    TEST_ASSERT_EQUAL(AG_OK, encode());
    TEST_ASSERT_EQUAL_UINT8(AG_UCCS_LEN, ag_writer_len(&w));
}

/*
 * The nested CoSWID is headed with its own length, and that head is written
 * before the CoSWID is emitted. A mismatch there produces a byte string that
 * claims more or fewer bytes than follow it, which a decoder reads as garbage
 * starting at the next claim -- so it is checked against the bytes rather than
 * against the constant alone.
 */
static void test_the_coswid_length_field_matches_what_follows_it(void)
{
    uint8_t i;

    TEST_ASSERT_EQUAL(AG_OK, encode());
    /* 0x58 is a byte string with a one-byte length; it is the last such head
     * in the claims set, immediately before the CoSWID. */
    i = (uint8_t)(AG_UCCS_LEN - AG_UCCS_COSWID_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x58u, buf[i - 2u]);
    TEST_ASSERT_EQUAL_HEX8(AG_UCCS_COSWID_LEN, buf[i - 1u]);
    /* and it is a CBOR map of five pairs, which is what RFC 9393 requires of
     * the smallest valid tag */
    TEST_ASSERT_EQUAL_HEX8(0xA5u, buf[i]);
}

/* The claim keys, read out of the encoded bytes rather than out of the header
 * that produced them. 10 is one byte; 256 and 273 are two-byte arguments, big
 * endian, which is the thing a little-endian codebase gets wrong. */
static void test_the_claim_keys_are_the_registered_ones(void)
{
    TEST_ASSERT_EQUAL(AG_OK, encode());
    TEST_ASSERT_EQUAL_HEX8(0xA3u, buf[0]);          /* map of three */
    TEST_ASSERT_EQUAL_HEX8(0x0Au, buf[1]);          /* nonce, key 10 */
    /* bstr(32): 32 is past the 23 that fits an initial byte, so the head is
     * 0x58 plus a one-byte argument -- and 0x58 rather than 0x59, because
     * deterministic CBOR takes the smallest head that fits. */
    TEST_ASSERT_EQUAL_HEX8(0x58u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x20u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x19u, buf[36]);         /* uint, two-byte arg */
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[37]);         /* 0x0100 = 256, ueid */
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[38]);
    TEST_ASSERT_EQUAL_HEX8(0x51u, buf[39]);         /* bstr(17): type byte + id */
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[40]);         /* UEID type RAND */
}

/* ----------------------------------------------------------- it is bound */

/* The nonce is the whole reason the extension exists, so a change to it must
 * reach the output. Everything before the nonce is constant, so the difference
 * has to appear inside it. */
static void test_a_different_nonce_changes_the_claims_set(void)
{
    uint8_t     other[AG_ALIAS_NONCE_LEN];
    uint8_t     first[AG_UCCS_LEN];
    ag_writer_t w2;

    TEST_ASSERT_EQUAL(AG_OK, encode());
    memcpy(first, buf, sizeof first);

    memcpy(other, NONCE, sizeof other);
    other[0] ^= 0xFFu;
    ag_writer_init(&w2, buf, AG_UCCS_LEN);
    TEST_ASSERT_EQUAL(AG_OK, ag_uccs_encode(&w2, other, AG_ALIAS_NONCE_LEN,
                                            UEID, FWHASH));
    TEST_ASSERT_FALSE(memcmp(first, buf, AG_UCCS_LEN) == 0);
    TEST_ASSERT_EQUAL_HEX8(first[4] ^ 0xFFu, buf[4]);   /* first nonce byte */
}

/* And so must a change to what was measured, or the evidence does not say what
 * it measured. */
static void test_a_different_firmware_hash_changes_the_claims_set(void)
{
    uint8_t     other[AG_SHA256_LEN];
    uint8_t     first[AG_UCCS_LEN];
    ag_writer_t w2;

    TEST_ASSERT_EQUAL(AG_OK, encode());
    memcpy(first, buf, sizeof first);

    memcpy(other, FWHASH, sizeof other);
    other[31] ^= 0x01u;
    ag_writer_init(&w2, buf, AG_UCCS_LEN);
    TEST_ASSERT_EQUAL(AG_OK, ag_uccs_encode(&w2, NONCE, AG_ALIAS_NONCE_LEN,
                                            UEID, other));
    TEST_ASSERT_FALSE(memcmp(first, buf, AG_UCCS_LEN) == 0);
}

/* The device identity likewise: two devices running the same firmware and
 * answering the same challenge must not produce the same evidence. */
static void test_a_different_device_id_changes_the_claims_set(void)
{
    uint8_t     other[AG_ATTEST_ID_LEN];
    uint8_t     first[AG_UCCS_LEN];
    ag_writer_t w2;

    TEST_ASSERT_EQUAL(AG_OK, encode());
    memcpy(first, buf, sizeof first);

    memcpy(other, UEID, sizeof other);
    other[0] ^= 0xFFu;
    ag_writer_init(&w2, buf, AG_UCCS_LEN);
    TEST_ASSERT_EQUAL(AG_OK, ag_uccs_encode(&w2, NONCE, AG_ALIAS_NONCE_LEN,
                                            other, FWHASH));
    TEST_ASSERT_FALSE(memcmp(first, buf, AG_UCCS_LEN) == 0);
}

/* ------------------------------------------------------------- refusals */

/*
 * A nonce of the wrong width. RFC 9711 permits 8 to 64 bytes and this encoder
 * emits exactly 32, because the certificate template has no room for a field
 * that changes length. A short nonce silently padded would be a claims set
 * asserting bytes the verifier never sent.
 */
static void test_a_nonce_of_the_wrong_length_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
                      ag_uccs_encode(&w, NONCE, 8u, UEID, FWHASH));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
                      ag_uccs_encode(&w, NONCE, 0u, UEID, FWHASH));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
                      ag_uccs_encode(&w, NONCE, 64u, UEID, FWHASH));
    /*
     * AND SIXTEEN IS REFUSED, which is the one worth spelling out. Sixteen is
     * the evidence frame's nonce width, so a caller that passed the wrong one
     * of this firmware's two nonces would otherwise produce a claims set that
     * encodes cleanly, is 17 bytes short of the template, and fails somewhere
     * downstream with a length error that names neither nonce.
     */
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
                      ag_uccs_encode(&w, NONCE, AG_ATTEST_NONCE_LEN, UEID, FWHASH));
    /* and it stopped before writing anything */
    TEST_ASSERT_EQUAL_UINT8(0u, ag_writer_len(&w));
}

/*
 * A buffer one byte short. The refusal must be AG_ERR_TOO_LONG and not a
 * truncated claims set reported as success -- a decoder handed 134 of 135 bytes
 * reads a well-formed map whose last value ran out, which is a parse failure
 * attributed to the wrong side of the link.
 */
static void test_a_buffer_one_byte_short_is_refused(void)
{
    ag_writer_t small;

    ag_writer_init(&small, buf, (uint8_t)(AG_UCCS_LEN - 1u));
    TEST_ASSERT_EQUAL(AG_ERR_TOO_LONG,
                      ag_uccs_encode(&small, NONCE, AG_ALIAS_NONCE_LEN,
                                     UEID, FWHASH));
    TEST_ASSERT_FALSE(ag_writer_ok(&small));
}

/* A buffer far too small fails the same way rather than differently: the
 * overflow is sticky, so the first refusal is the only one that matters and
 * every later write is refused too. */
static void test_a_buffer_far_too_small_is_refused(void)
{
    ag_writer_t tiny;

    ag_writer_init(&tiny, buf, 4u);
    TEST_ASSERT_EQUAL(AG_ERR_TOO_LONG,
                      ag_uccs_encode(&tiny, NONCE, AG_ALIAS_NONCE_LEN,
                                     UEID, FWHASH));
}

/* The exact size is enough, and the byte past it is untouched -- the bound is
 * a bound, not an approximation. */
static void test_a_buffer_of_exactly_the_right_size_is_accepted(void)
{
    TEST_ASSERT_EQUAL(AG_OK, encode());
    TEST_ASSERT_TRUE(ag_writer_ok(&w));
    TEST_ASSERT_EQUAL_HEX8(0xCCu, buf[AG_UCCS_LEN]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_claims_set_is_the_bytes_the_vector_pins);
    RUN_TEST(test_the_declared_length_is_the_length_emitted);
    RUN_TEST(test_the_coswid_length_field_matches_what_follows_it);
    RUN_TEST(test_the_claim_keys_are_the_registered_ones);
    RUN_TEST(test_a_different_nonce_changes_the_claims_set);
    RUN_TEST(test_a_different_firmware_hash_changes_the_claims_set);
    RUN_TEST(test_a_different_device_id_changes_the_claims_set);
    RUN_TEST(test_a_nonce_of_the_wrong_length_is_refused);
    RUN_TEST(test_a_buffer_one_byte_short_is_refused);
    RUN_TEST(test_a_buffer_far_too_small_is_refused);
    RUN_TEST(test_a_buffer_of_exactly_the_right_size_is_accepted);
    return UNITY_END();
}
