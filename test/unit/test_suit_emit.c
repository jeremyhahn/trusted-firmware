/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The build's SUIT emitter (tools/suit-emit/, issue #78) against the literal
 * vectors: the same image, sequence and board must give the same manifest,
 * the same to-be-signed bytes and -- around the vectors' signature -- the
 * same envelope, byte for byte. Built with the ATmega2560's profile, whose
 * class id the vectors carry.
 */
#include <string.h>
#include "unity.h"
#include "emit.h"
#include "tfw/config.h"
#include "suit_vectors.h"

/* Where the signature sits in SUIT_ENVELOPE; test_suit.c pins the offset. */
#define SIG_OFFSET 57u

static const uint8_t vendor[16]   = TFW_SUIT_VENDOR_ID;
static const uint8_t class_id[16] = TFW_SUIT_CLASS_ID;

void setUp(void) {}
void tearDown(void) {}

static void test_the_manifest_is_the_vectors(void)
{
    uint8_t out[256];
    size_t  n = tfw_suit_emit_manifest(out, sizeof out, SUIT_IMAGE_DIGEST,
                                       sizeof SUIT_IMAGE, 7u, vendor, class_id);

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_MANIFEST, n);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_MANIFEST, out, sizeof SUIT_MANIFEST);
}

static void test_the_tbs_is_the_vectors(void)
{
    uint8_t out[128];
    size_t  n = tfw_suit_emit_tbs(out, sizeof out, SUIT_MANIFEST, sizeof SUIT_MANIFEST);

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_TBS, n);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_TBS, out, sizeof SUIT_TBS);
}

static void test_the_envelope_is_the_vectors(void)
{
    uint8_t out[320];
    size_t  n = tfw_suit_emit_envelope(out, sizeof out, SUIT_MANIFEST, sizeof SUIT_MANIFEST,
                                       &SUIT_ENVELOPE[SIG_OFFSET]);

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_ENVELOPE, n);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_ENVELOPE, out, sizeof SUIT_ENVELOPE);
}

/* The CBOR heads wider than one byte (RFC 8949 §3): seq 0x0100 takes the
 * two-byte form 19 01 00, image size 0x01020304 the four-byte 1A 01 02 03 04.
 * Every other byte is the vector's, with the three bstr lengths each grown by
 * the bytes inside them: the manifest 0x62 -> 0x67, the common 0x5a -> 0x5d,
 * the shared sequence 0x51 -> 0x54. */
static void test_wide_heads_are_the_vectors_spliced(void)
{
    static const uint8_t head[26] = {
        0x58, 0x67, 0xa3, 0x01, 0x01, 0x02, 0x19, 0x01, 0x00, 0x03, 0x58, 0x5d,
        0xa2, 0x02, 0x81, 0x81, 0x41, 0x00, 0x04, 0x58, 0x54, 0x82, 0x14, 0xa4,
        0x01, 0x50,
    };
    static const uint8_t tail[5] = { 0x1a, 0x01, 0x02, 0x03, 0x04 };
    static const uint8_t vector_tail[3] = { 0x0e, 0x18, 0x40 };
    uint8_t out[256];
    size_t  n = tfw_suit_emit_manifest(out, sizeof out, SUIT_IMAGE_DIGEST,
                                       0x01020304uL, 0x0100u, vendor, class_id);

    /* The vector ends in key 14 and the 64-byte image's size, 18 40. */
    TEST_ASSERT_EQUAL_MEMORY(vector_tail, &SUIT_MANIFEST[97], sizeof vector_tail);
    TEST_ASSERT_EQUAL_size_t(105u, n);
    TEST_ASSERT_EQUAL_MEMORY(head, out, sizeof head);
    TEST_ASSERT_EQUAL_MEMORY(&SUIT_MANIFEST[24], &out[26], 74u);
    TEST_ASSERT_EQUAL_MEMORY(tail, &out[100], sizeof tail);
}

/* The integrated-payload envelope: the policy class is not a board's, so it is
 * spelled here; UUIDv5(vendor, "atmega2560/policy"). */
static const uint8_t policy_class[16] = {
    0xd3, 0x7d, 0x83, 0xb8, 0x9b, 0xb6, 0x52, 0x35,
    0x99, 0xae, 0x46, 0xe1, 0x79, 0xd5, 0x11, 0x99,
};

static void test_the_integrated_manifest_is_the_vectors(void)
{
    uint8_t out[256];
    size_t  n = tfw_suit_emit_manifest_integrated(out, sizeof out, SUIT_POLICY_DIGEST,
                                                  sizeof SUIT_POLICY_BODY, 3u, vendor,
                                                  policy_class, "#policy");

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_POLICY_MANIFEST, n);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_POLICY_MANIFEST, out, sizeof SUIT_POLICY_MANIFEST);
    n = tfw_suit_emit_tbs(out, sizeof out, SUIT_POLICY_MANIFEST, sizeof SUIT_POLICY_MANIFEST);
    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_POLICY_TBS, n);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_POLICY_TBS, out, sizeof SUIT_POLICY_TBS);
}

static void test_the_integrated_envelopes_are_the_vectors(void)
{
    uint8_t out[320];
    size_t  n = tfw_suit_emit_envelope_integrated(out, sizeof out, SUIT_POLICY_MANIFEST,
                                                  sizeof SUIT_POLICY_MANIFEST,
                                                  &SUIT_POLICY_ENVELOPE[SIG_OFFSET], "#policy",
                                                  SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY);

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_POLICY_ENVELOPE, n);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_POLICY_ENVELOPE, out, sizeof SUIT_POLICY_ENVELOPE);
    n = tfw_suit_emit_envelope(out, sizeof out, SUIT_POLICY_MANIFEST, sizeof SUIT_POLICY_MANIFEST,
                               &SUIT_POLICY_ENVELOPE[SIG_OFFSET]);
    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_POLICY_ENVELOPE_DETACHED, n);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_POLICY_ENVELOPE_DETACHED, out,
                             sizeof SUIT_POLICY_ENVELOPE_DETACHED);
}

static void test_an_integrated_buffer_too_small_writes_nothing_it_claims(void)
{
    uint8_t out[sizeof SUIT_POLICY_ENVELOPE];

    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_manifest_integrated(
                                     out, sizeof SUIT_POLICY_MANIFEST - 1u, SUIT_POLICY_DIGEST,
                                     sizeof SUIT_POLICY_BODY, 3u, vendor, policy_class,
                                     "#policy"));
    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_envelope_integrated(
                                     out, sizeof SUIT_POLICY_ENVELOPE - 1u, SUIT_POLICY_MANIFEST,
                                     sizeof SUIT_POLICY_MANIFEST,
                                     &SUIT_POLICY_ENVELOPE[SIG_OFFSET], "#policy",
                                     SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_integrated_manifest_is_the_vectors);
    RUN_TEST(test_the_integrated_envelopes_are_the_vectors);
    RUN_TEST(test_an_integrated_buffer_too_small_writes_nothing_it_claims);
    RUN_TEST(test_the_manifest_is_the_vectors);
    RUN_TEST(test_the_tbs_is_the_vectors);
    RUN_TEST(test_the_envelope_is_the_vectors);
    RUN_TEST(test_wide_heads_are_the_vectors_spliced);
    return UNITY_END();
}
