/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw_crypto_verify_operator in software (src/crypto/soft/verify_p256.c), the
 * verify an HSM image checks a policy set with (#50, m7-design.md slice 3).
 *
 * The genuine case is a literal: SUIT_POLICY_ENVELOPE's COSE_Sign1 signature
 * (testdata/suit_vectors.h), made outside this tree under the throwaway tenant
 * key, over SHA-256 of SUIT_POLICY_TBS. Every other case changes one input.
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_tenant_signature_verifies);
    RUN_TEST(test_a_flipped_signature_bit_is_refused);
    RUN_TEST(test_a_flipped_digest_bit_is_refused);
    RUN_TEST(test_another_key_is_refused);
    RUN_TEST(test_a_point_off_the_curve_is_refused);
    RUN_TEST(test_a_missing_argument_is_refused);
    return UNITY_END();
}
