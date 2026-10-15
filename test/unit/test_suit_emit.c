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

static void test_a_buffer_too_small_writes_nothing_it_claims(void)
{
    uint8_t out[sizeof SUIT_ENVELOPE - 1u];

    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_manifest(out, sizeof SUIT_MANIFEST - 1u,
                                                        SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE,
                                                        7u, vendor, class_id));
    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_tbs(out, sizeof SUIT_TBS - 1u,
                                                   SUIT_MANIFEST, sizeof SUIT_MANIFEST));
    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_envelope(out, sizeof out, SUIT_MANIFEST,
                                                        sizeof SUIT_MANIFEST,
                                                        &SUIT_ENVELOPE[SIG_OFFSET]));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_manifest_is_the_vectors);
    RUN_TEST(test_the_tbs_is_the_vectors);
    RUN_TEST(test_the_envelope_is_the_vectors);
    RUN_TEST(test_a_buffer_too_small_writes_nothing_it_claims);
    return UNITY_END();
}
