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
#include "vectors.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"

static const uint8_t NONCE[]  = TFWV_UCCS_NONCE;
static const uint8_t UEID[]   = TFWV_UCCS_UEID;
static const uint8_t FWHASH[] = TFWV_UCCS_FWHASH;
static const uint8_t WANT[]   = TFWV_UCCS;
static const uint8_t APPEV[]  = TFWV_APPEV_EXAMPLE;
static const uint8_t WITH[]   = TFWV_APPEV_WITH_UCCS;
static const uint8_t WITHOUT[] = TFWV_APPEV_WITHOUT_UCCS;

static uint8_t     buf[TFW_UCCS_LEN + 8u];
static tfw_writer_t w;

void setUp(void)
{
    memset(buf, 0xCC, sizeof buf);
    tfw_writer_init(&w, buf, TFW_UCCS_LEN);
}

void tearDown(void) {}

static tfw_status_t encode(void)
{
    return tfw_uccs_encode(&w, NONCE, TFW_ALIAS_NONCE_LEN, UEID, FWHASH);
}

/* ------------------------------------------------------------ the vector */

/*
 * THE CONTRACT. Every byte, in order. This is the value that was decoded with
 * an independent CBOR implementation and found to carry claim keys 10, 256 and
 * 273 and nothing else, with no trailing bytes.
 */
static void test_the_claims_set_is_the_bytes_the_vector_pins(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, encode());
    TEST_ASSERT_EQUAL_UINT8(TFWV_UCCS_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, TFWV_UCCS_LEN);
}

/* #51: an application's 32 bytes take the firmware measurement's place in 273,
 * and nothing else moves -- same length, same keys, same CoSWID. */
static void test_application_evidence_is_the_bytes_the_with_vector_pins(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_uccs_encode(&w, NONCE, TFW_ALIAS_NONCE_LEN, UEID, APPEV));
    TEST_ASSERT_EQUAL_UINT8(TFWV_APPEV_UCCS_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WITH, buf, TFWV_APPEV_UCCS_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(APPEV, &buf[TFWV_APPEV_UCCS_HASH_AT], 32u);
}

/* And with none, the claims set is the one every image without an application
 * has always emitted. */
static void test_no_application_evidence_is_the_bytes_the_without_vector_pins(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, encode());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WITHOUT, buf, TFWV_APPEV_UCCS_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, WITHOUT, TFWV_UCCS_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FWHASH, &buf[TFWV_APPEV_UCCS_HASH_AT], 32u);
}

/* The constant the certificate template is built around. If the encoder and the
 * template disagree about this number the certificate's DER lengths describe a
 * different document than the one that follows them. */
static void test_the_declared_length_is_the_length_emitted(void)
{
    TEST_ASSERT_EQUAL_UINT8(TFW_UCCS_LEN, TFWV_UCCS_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, encode());
    TEST_ASSERT_EQUAL_UINT8(TFW_UCCS_LEN, tfw_writer_len(&w));
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

    TEST_ASSERT_EQUAL(TFW_OK, encode());
    /* 0x58 is a byte string with a one-byte length; it is the last such head
     * in the claims set, immediately before the CoSWID. */
    i = (uint8_t)(TFW_UCCS_LEN - TFW_UCCS_COSWID_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x58u, buf[i - 2u]);
    TEST_ASSERT_EQUAL_HEX8(TFW_UCCS_COSWID_LEN, buf[i - 1u]);
    /* and it is a CBOR map of five pairs, which is what RFC 9393 requires of
     * the smallest valid tag */
    TEST_ASSERT_EQUAL_HEX8(0xA5u, buf[i]);
}

/* The claim keys, read out of the encoded bytes rather than out of the header
 * that produced them. 10 is one byte; 256 and 273 are two-byte arguments, big
 * endian, which is the thing a little-endian codebase gets wrong. */
static void test_the_claim_keys_are_the_registered_ones(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, encode());
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

/* ------------------------------------------------------------- refusals */

/*
 * A buffer one byte short. The refusal must be TFW_ERR_TOO_LONG and not a
 * truncated claims set reported as success -- a decoder handed 134 of 135 bytes
 * reads a well-formed map whose last value ran out, which is a parse failure
 * attributed to the wrong side of the link.
 */
static void test_a_buffer_one_byte_short_is_refused(void)
{
    tfw_writer_t small;

    tfw_writer_init(&small, buf, (uint8_t)(TFW_UCCS_LEN - 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_TOO_LONG,
                      tfw_uccs_encode(&small, NONCE, TFW_ALIAS_NONCE_LEN,
                                     UEID, FWHASH));
    TEST_ASSERT_FALSE(tfw_writer_ok(&small));
}

/* The exact size is enough, and the byte past it is untouched -- the bound is
 * a bound, not an approximation. */
static void test_a_buffer_of_exactly_the_right_size_is_accepted(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, encode());
    TEST_ASSERT_TRUE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_HEX8(0xCCu, buf[TFW_UCCS_LEN]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_claims_set_is_the_bytes_the_vector_pins);
    RUN_TEST(test_the_declared_length_is_the_length_emitted);
    RUN_TEST(test_the_coswid_length_field_matches_what_follows_it);
    RUN_TEST(test_the_claim_keys_are_the_registered_ones);
    RUN_TEST(test_a_buffer_one_byte_short_is_refused);
    RUN_TEST(test_a_buffer_of_exactly_the_right_size_is_accepted);
    RUN_TEST(test_application_evidence_is_the_bytes_the_with_vector_pins);
    RUN_TEST(test_no_application_evidence_is_the_bytes_the_without_vector_pins);
    return UNITY_END();
}
