/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The software element's curve, pinned against facts that do not come from this
 * repository.
 *
 * test/model/atecc608a.c is now a test ORACLE: the evidence tfwsim-atecc608a
 * produces is meant to verify under independent implementations, and a
 * model that is wrong in a self-consistent way would make every suite built on
 * it wrong the same way. So its primitives are checked here against RFC 6979
 * A.2.5 (P-256, SHA-256, message "sample") -- its public point, and its
 * published signature, which the model's Verify must accept -- and against the
 * documented public point of the TEST key, which was derived from its seed with
 * a different library (Python cryptography) before being written down. Sign's
 * k is micro-ecc's HMAC-DRBG variant of RFC 6979, which does not reproduce the
 * RFC's signature bytes, so signing is pinned by verification and by
 * determinism instead.
 *
 * This suite links the model and nothing of the firmware's crypto tier. The
 * element's use of these primitives through the real driver is test_atecc.c.
 */
#include <string.h>
#include "unity.h"
#include "atecc608a_model.h"
#include "sha256.h"

void setUp(void)    { tfw_model_reset(); }
void tearDown(void) {}

/* RFC 6979 A.2.5. */
static const uint8_t RFC_PRIV[32] = {
    0xC9,0xAF,0xA9,0xD8,0x45,0xBA,0x75,0x16,0x6B,0x5C,0x21,0x57,0x67,0xB1,0xD6,0x93,
    0x4E,0x50,0xC3,0xDB,0x36,0xE8,0x9B,0x12,0x7B,0x8A,0x62,0x2B,0x12,0x0F,0x67,0x21
};
static const uint8_t RFC_PUB[64] = {
    0x60,0xFE,0xD4,0xBA,0x25,0x5A,0x9D,0x31,0xC9,0x61,0xEB,0x74,0xC6,0x35,0x6D,0x68,
    0xC0,0x49,0xB8,0x92,0x3B,0x61,0xFA,0x6C,0xE6,0x69,0x62,0x2E,0x60,0xF2,0x9F,0xB6,
    0x79,0x03,0xFE,0x10,0x08,0xB8,0xBC,0x99,0xA4,0x1A,0xE9,0xE9,0x56,0x28,0xBC,0x64,
    0xF2,0xF1,0xB2,0x0C,0x2D,0x7E,0x9F,0x51,0x77,0xA3,0xC2,0x94,0xD4,0x46,0x22,0x99
};
static const uint8_t RFC_SAMPLE_SIG[64] = {   /* r || s, SHA-256, "sample" */
    0xEF,0xD4,0x8B,0x2A,0xAC,0xB6,0xA8,0xFD,0x11,0x40,0xDD,0x9C,0xD4,0x5E,0x81,0xD6,
    0x9D,0x2C,0x87,0x7B,0x56,0xAA,0xF9,0x91,0xC3,0x4D,0x0E,0xA8,0x4E,0xAF,0x37,0x16,
    0xF7,0xCB,0x1C,0x94,0x2D,0x65,0x7C,0x41,0xD4,0x36,0xC7,0xA1,0xB6,0xE2,0x9F,0x65,
    0xF3,0xE9,0x00,0xDB,0xB9,0xAF,0xF4,0x06,0x4D,0xC4,0xAB,0x2F,0x84,0x3A,0xCD,0xA8
};

/* The TEST DeviceID key: private = SHA-256("tfwsim-atecc608a TEST DeviceID key v1"),
 * and the public point below, as published in docs/dice-attestation/. */
static const uint8_t TEST_PRIV[32] = {
    0x28,0x37,0x06,0xF5,0x76,0x29,0xC2,0xDA,0xA7,0xB1,0x61,0xE5,0xE6,0x42,0x30,0x77,
    0xE6,0x52,0x60,0x18,0x7F,0xBB,0xD8,0x67,0x34,0x73,0xEF,0x7A,0xEA,0x81,0x55,0x1D
};
static const uint8_t TEST_PUB[64] = {
    0x41,0x03,0x50,0x35,0xFA,0xF7,0x82,0xEF,0x1C,0xAB,0x6A,0x66,0x9F,0x1C,0x38,0x09,
    0xB8,0x9F,0xBF,0x1C,0xD0,0x96,0x77,0x1D,0x12,0xD8,0x7E,0x1C,0xB3,0xC3,0x5A,0xCA,
    0xED,0xA3,0xE5,0x02,0x1F,0xF3,0xEE,0x9E,0x48,0x32,0x9B,0xCC,0x44,0xC1,0x06,0x1E,
    0x1D,0x59,0x49,0xF1,0xFB,0xBA,0x07,0x42,0xA5,0xB6,0xAC,0x67,0x99,0xEF,0xE7,0x8A
};

static void sample_digest(uint8_t d[32])
{
    tfw_sha256((const uint8_t *)"sample", 6u, d);
}

static void test_a_private_scalar_yields_the_rfc_6979_public_point(void)
{
    uint8_t pub[64];

    TEST_ASSERT_TRUE(tfw_model_p256_public(RFC_PRIV, pub));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC_PUB, pub, 64);
}

static void test_a_signature_made_with_the_rfc_key_verifies_under_its_public_point(void)
{
    uint8_t d[32], sig[64];

    sample_digest(d);
    TEST_ASSERT_TRUE(tfw_model_p256_sign(RFC_PRIV, d, sig));
    TEST_ASSERT_TRUE(tfw_model_p256_verify(RFC_PUB, d, sig));
    /* Not the RFC's own bytes, and not meant to be: see the header. It is a
     * different valid signature for the same key and digest. */
    TEST_ASSERT_FALSE(tfw_model_p256_verify(TEST_PUB, d, sig));
}

static void test_the_rfc_6979_signature_verifies(void)
{
    uint8_t d[32];

    sample_digest(d);
    TEST_ASSERT_TRUE(tfw_model_p256_verify(RFC_PUB, d, RFC_SAMPLE_SIG));
}

static void test_verification_rejects_a_changed_digest_signature_or_key(void)
{
    uint8_t d[32], sig[64], bad_pub[64];

    sample_digest(d);
    memcpy(sig, RFC_SAMPLE_SIG, 64u);

    d[31] ^= 0x01u;
    TEST_ASSERT_FALSE(tfw_model_p256_verify(RFC_PUB, d, sig));
    d[31] ^= 0x01u;

    sig[0] ^= 0x80u;
    TEST_ASSERT_FALSE(tfw_model_p256_verify(RFC_PUB, d, sig));
    sig[0] ^= 0x80u;
    sig[63] ^= 0x01u;
    TEST_ASSERT_FALSE(tfw_model_p256_verify(RFC_PUB, d, sig));

    /* A different, valid key. */
    TEST_ASSERT_FALSE(tfw_model_p256_verify(TEST_PUB, d, RFC_SAMPLE_SIG));

    /* A point that is not on the curve is no key at all. */
    memcpy(bad_pub, RFC_PUB, 64u);
    bad_pub[63] ^= 0x01u;
    TEST_ASSERT_FALSE(tfw_model_p256_verify(bad_pub, d, RFC_SAMPLE_SIG));
}

static void test_verification_rejects_an_all_zero_signature(void)
{
    uint8_t d[32], zero[64];

    sample_digest(d);
    memset(zero, 0, sizeof zero);
    TEST_ASSERT_FALSE(tfw_model_p256_verify(RFC_PUB, d, zero));
}

static void test_signing_refuses_an_invalid_private_scalar(void)
{
    uint8_t d[32], sig[64], zero[32];

    sample_digest(d);
    memset(zero, 0, sizeof zero);
    TEST_ASSERT_FALSE(tfw_model_p256_sign(zero, d, sig));
    TEST_ASSERT_FALSE(tfw_model_p256_public(zero, sig));
}

/* ----------------------------------------------------- the test identity */

static void test_the_test_key_is_the_documented_one(void)
{
    uint8_t priv[32], pub[64], seed_hash[32];

    tfw_model_test_deviceid(priv, pub);
    tfw_sha256((const uint8_t *)"tfwsim-atecc608a TEST DeviceID key v1", 37u, seed_hash);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(seed_hash, priv, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST_PRIV, priv, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST_PUB, pub, 64);
}

static void test_the_test_keys_public_point_matches_its_private_scalar(void)
{
    uint8_t pub[64];

    TEST_ASSERT_TRUE(tfw_model_p256_public(TEST_PRIV, pub));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST_PUB, pub, 64);
}

static void test_no_key_exists_until_the_element_is_provisioned(void)
{
    TEST_ASSERT_NULL(tfw_model_deviceid_public());
    TEST_ASSERT_NULL(tfw_model_deviceid_private());

    tfw_model_provision();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST_PUB, tfw_model_deviceid_public(), 64);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST_PRIV, tfw_model_deviceid_private(), 32);

    tfw_model_reset();
    TEST_ASSERT_NULL(tfw_model_deviceid_public());
}

static void test_signing_the_same_digest_twice_gives_the_same_signature(void)
{
    uint8_t d[32], a[64], b[64];

    memset(d, 0x5A, sizeof d);
    TEST_ASSERT_TRUE(tfw_model_p256_sign(TEST_PRIV, d, a));
    TEST_ASSERT_TRUE(tfw_model_p256_sign(TEST_PRIV, d, b));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, b, 64);
    d[0] ^= 1u;
    TEST_ASSERT_TRUE(tfw_model_p256_sign(TEST_PRIV, d, b));
    TEST_ASSERT_FALSE(memcmp(a, b, 64u) == 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_private_scalar_yields_the_rfc_6979_public_point);
    RUN_TEST(test_a_signature_made_with_the_rfc_key_verifies_under_its_public_point);
    RUN_TEST(test_the_rfc_6979_signature_verifies);
    RUN_TEST(test_verification_rejects_a_changed_digest_signature_or_key);
    RUN_TEST(test_verification_rejects_an_all_zero_signature);
    RUN_TEST(test_signing_refuses_an_invalid_private_scalar);
    RUN_TEST(test_the_test_key_is_the_documented_one);
    RUN_TEST(test_the_test_keys_public_point_matches_its_private_scalar);
    RUN_TEST(test_no_key_exists_until_the_element_is_provisioned);
    RUN_TEST(test_signing_the_same_digest_twice_gives_the_same_signature);
    return UNITY_END();
}
