/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw_crypto_verify_operator in software (src/crypto/soft/verify_p256.c), the
 * verify an HSM image checks a policy set with (#50, m7-design.md slice 3).
 *
 * The genuine case is a literal: SUIT_POLICY_ENVELOPE's COSE_Sign1 signature
 * (testdata/suit_vectors.h), made outside this tree under the throwaway tenant
 * key, over SHA-256 of SUIT_POLICY_TBS. Every other case changes one input.
 * The cases that set r or s to zero or to the group order n are FIPS 186-5
 * section 6.4.2 step 1.
 */
#include <string.h>
#include "unity.h"
#include "tfw/crypto.h"
#include "sha256.h"
#include "suit_vectors.h"

/* The envelope's signature: bstr(64), 0x58 0x40, at byte 55. */
#define SIG_AT 57u

static uint8_t digest[32];
static uint8_t sig[64];
static uint8_t pub[64];

void setUp(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x58u, SUIT_POLICY_ENVELOPE[SIG_AT - 2u]);
    TEST_ASSERT_EQUAL_HEX8(0x40u, SUIT_POLICY_ENVELOPE[SIG_AT - 1u]);
    tfw_sha256(SUIT_POLICY_TBS, sizeof SUIT_POLICY_TBS, digest);
    memcpy(sig, &SUIT_POLICY_ENVELOPE[SIG_AT], sizeof sig);
    memcpy(pub, SUIT_TENANT_PUB, sizeof pub);
}

void tearDown(void) {}

static void test_the_tenant_signature_verifies(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_operator(digest, sig, pub));
}

static void test_a_flipped_signature_bit_is_refused(void)
{
    sig[63] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
}

static void test_a_flipped_digest_bit_is_refused(void)
{
    digest[0] ^= 0x80u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
}

/* The image key is a valid point, but not the key this was signed under. */
static void test_another_key_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, SUIT_PUB));
}

static void test_a_point_off_the_curve_is_refused(void)
{
    pub[63] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
    memset(pub, 0, sizeof pub);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
}

static void test_a_missing_argument_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(NULL, sig, pub));
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, NULL, pub));
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, NULL));
}

/* The P-256 group order n, big-endian (SEC 2 section 2.4.2). */
static const uint8_t P256_N[32] = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x00u, 0x00u, 0x00u, 0x00u,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xBCu, 0xE6u, 0xFAu, 0xADu, 0xA7u, 0x17u, 0x9Eu, 0x84u,
    0xF3u, 0xB9u, 0xCAu, 0xC2u, 0xFCu, 0x63u, 0x25u, 0x51u,
};

static void test_a_zero_r_is_refused(void)
{
    memset(sig, 0, 32u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
}

static void test_a_zero_s_is_refused(void)
{
    memset(&sig[32], 0, 32u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
}

static void test_an_r_equal_to_the_order_is_refused(void)
{
    memcpy(sig, P256_N, sizeof P256_N);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
}

static void test_an_s_equal_to_the_order_is_refused(void)
{
    memcpy(&sig[32], P256_N, sizeof P256_N);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_tenant_signature_verifies);
    RUN_TEST(test_a_flipped_signature_bit_is_refused);
    RUN_TEST(test_a_flipped_digest_bit_is_refused);
    RUN_TEST(test_another_key_is_refused);
    RUN_TEST(test_a_point_off_the_curve_is_refused);
    RUN_TEST(test_a_missing_argument_is_refused);
    RUN_TEST(test_a_zero_r_is_refused);
    RUN_TEST(test_a_zero_s_is_refused);
    RUN_TEST(test_an_r_equal_to_the_order_is_refused);
    RUN_TEST(test_an_s_equal_to_the_order_is_refused);
    return UNITY_END();
}
