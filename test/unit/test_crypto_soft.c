/*
 * The software crypto provider, src/crypto/soft/provider.c, driven directly --
 * issue #130. Every public function and every refusal path, with the exact
 * status and, where there is output, the exact bytes.
 *
 * The expected digests and MACs are literals computed outside this code
 * (Python's hmac/hashlib), never by the provider they pin.
 */
#include <string.h>
#include "unity.h"
#include "uECC.h"
#include "hal_mock.h"
#include "uds_mock.h"
#include "boot_soft_mock.h"
#include "handoff.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "boot_measure.h"

/* uds[i] = i + 1 */
static uint8_t TEST_UDS[32];
/* cdi[i] = 0xD0 + i, handed off by a tfw-boot */
static uint8_t TEST_CDI[32];

static const uint8_t ABC[3] = { 'a', 'b', 'c' };

/* SHA-256("abc"), FIPS 180-2 B.1. */
static const uint8_t SHA_ABC[32] = {
    0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
    0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
};

/* HMAC-SHA256(TEST_CDI, "abc") */
static const uint8_t HANDOFF_HMAC_ABC[32] = {
    0x45, 0xAF, 0xE7, 0xF1, 0x4B, 0xD8, 0xD8, 0x3D, 0xE1, 0x32, 0x50, 0xA8, 0xCD, 0xFE, 0xE4, 0x97,
    0xEB, 0x8B, 0x7A, 0x34, 0xBB, 0x84, 0x2E, 0x1F, 0x71, 0xCE, 0x7D, 0x69, 0x6A, 0x35, 0xBD, 0xAF
};

/* HMAC-SHA256(HMAC-SHA256(TEST_UDS, SHA-256("tfw-unmeasured-application-v1")), "abc") */
static const uint8_t FALLBACK_HMAC_ABC[32] = {
    0xE0, 0xAB, 0xB7, 0x8B, 0x6C, 0x14, 0xFA, 0x18, 0xEC, 0xB7, 0x73, 0x75, 0xDC, 0x75, 0x49, 0xC4,
    0x03, 0xA6, 0xCD, 0x31, 0x3D, 0xB8, 0x8C, 0x87, 0xC3, 0x2B, 0xC9, 0xF2, 0x4B, 0x7D, 0x8F, 0x6D
};

void setUp(void)
{
    for (uint8_t i = 0u; i < sizeof TEST_UDS; i++) { TEST_UDS[i] = (uint8_t)(i + 1u); }
    for (uint8_t i = 0u; i < sizeof TEST_CDI; i++) { TEST_CDI[i] = (uint8_t)(0xD0u + i); }
    tfw_mock_reset();
    tfw_boot_handoff_init();        /* the zeroed handoff area: no valid handoff */
    tfw_mock_set_uds(0);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
}

void tearDown(void) {}

static void hand_off(uint8_t tier, const uint8_t cdi[32])
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t measurement[32];

    for (uint8_t i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    tfw_boot_handoff_build(block, tier, cdi, measurement, 1u, 0u);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();
}

/* ---- identity of the tier ------------------------------------------------ */

static void test_the_tier_is_software(void)
{
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
}

static void test_the_only_algorithm_is_es256(void)
{
    TEST_ASSERT_EQUAL_HEX16(TFW_ALGBIT_ECDSA_P256_SHA256, tfw_crypto_algorithms());
}

/* ---- refusals ------------------------------------------------------------ */

static void test_random_is_refused_and_the_buffer_untouched(void)
{
    uint8_t out[16];

    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_random(out, (uint8_t)sizeof out));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof out);
}

static void test_the_public_key_is_the_uds_deviceid_point(void)
{
    uint8_t out[64], again[64];

    tfw_mock_boot_soft(TEST_UDS, TEST_CDI);   /* the seed comes in the handoff */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(out));
    TEST_ASSERT_TRUE(uECC_valid_public_key(out, uECC_secp256r1()));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(again));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(out, again, 64);
}

static void test_no_uds_has_no_public_key_and_the_buffer_untouched(void)
{
    uint8_t out[64];

    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_pubkey(out));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof out);
}

static void test_there_is_no_counter_and_the_value_untouched(void)
{
    uint32_t n = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_counter(0u, &n));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, n);
}

/* provider.c:250-256 answers before reading anything: NULL for every argument
 * would fault if it read the digest, the signature or the key. */
static void test_operator_verify_is_refused_without_reading_the_key(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_operator(0, 0, 0));
}

static void test_image_verify_is_refused_without_reading_its_arguments(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_image(0, 0));
}

static void test_hmac_refuses_any_slot_but_zero(void)
{
    uint8_t out[32];

    tfw_mock_set_uds(TEST_UDS);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(1u, ABC, sizeof ABC, out));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(0xFFu, ABC, sizeof ABC, out));
}

/* The slot is checked before provisioning (provider.c:187-192). */
static void test_hmac_checks_the_slot_before_provisioning(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(1u, ABC, sizeof ABC, out));
}

static void test_hmac_without_a_uds_is_not_provisioned(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, ABC, sizeof ABC, out));
}

static void test_sign_without_a_uds_is_not_provisioned_and_writes_nothing(void)
{
    uint8_t digest[32], sig[64];
    uint8_t alg = 0xEEu;

    memset(digest, 0x42, sizeof digest);
    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, sig, sizeof sig);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, alg);
}

/* ---- sha256 -------------------------------------------------------------- */

static void test_sha256_of_abc(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(ABC, sizeof ABC, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_ABC, out, 32);
}

/* No key is involved: an unprovisioned device hashes all the same. */
static void test_sha256_needs_no_provisioning(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, ABC, sizeof ABC, out));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(ABC, sizeof ABC, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_ABC, out, 32);
}

/* ---- init: the local fallback -------------------------------------------- */

static void test_no_handoff_derives_the_cdi_from_the_local_uds(void)
{
    uint8_t out[32];

    tfw_mock_set_uds(TEST_UDS);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, ABC, sizeof ABC, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FALLBACK_HMAC_ABC, out, 32);
}

/* init starts unprovisioned every time (provider.c:69): a UDS withdrawn
 * between two inits leaves nothing behind. */
static void test_a_second_init_without_a_uds_unprovisions(void)
{
    uint8_t out[32];

    tfw_mock_set_uds(TEST_UDS);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_mock_set_uds(0);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, ABC, sizeof ABC, out));
}

/* ---- init: the handoff --------------------------------------------------- */

static void test_a_software_handoff_cdi_keys_the_hmac(void)
{
    uint8_t out[32];

    tfw_mock_set_uds(TEST_UDS);         /* present, and ignored */
    hand_off(TFW_CRYPTO_SOFTWARE, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, ABC, sizeof ABC, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HANDOFF_HMAC_ABC, out, 32);
}

static void test_a_mismatched_tier_handoff_refuses_even_with_a_local_uds(void)
{
    uint8_t out[32];

    tfw_mock_set_uds(TEST_UDS);
    hand_off(TFW_CRYPTO_SECURE_ELEMENT, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, ABC, sizeof ABC, out));
}

/* The mismatched CDI is drained (provider.c:126-129): a later init finds it
 * gone, the handoff's tier still names the element, and stays unprovisioned. */
static void test_a_mismatched_tier_cdi_is_drained(void)
{
    uint8_t taken[32];
    uint8_t out[32];

    tfw_mock_set_uds(TEST_UDS);
    hand_off(TFW_CRYPTO_SECURE_ELEMENT, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_take_cdi(taken));
    TEST_ASSERT_NOT_NULL(tfw_boot_handoff_tier());
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SECURE_ELEMENT, *tfw_boot_handoff_tier());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, ABC, sizeof ABC, out));
}

static void test_a_software_handoff_with_the_zero_sentinel_refuses_even_with_a_local_uds(void)
{
    uint8_t zero[32];
    uint8_t out[32];

    memset(zero, 0, sizeof zero);
    tfw_mock_set_uds(TEST_UDS);
    hand_off(TFW_CRYPTO_SOFTWARE, zero);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, ABC, sizeof ABC, out));
}

/* ---- sign ---------------------------------------------------------------- */

static void test_sign_is_es256_and_verifies_under_the_public_key(void)
{
    uint8_t digest[32], sig[64], pub[64];
    uint8_t alg = TFW_ALG_NONE;

    tfw_mock_boot_soft(TEST_UDS, TEST_CDI);   /* the seed comes in the handoff */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    memset(digest, 0x42, sizeof digest);
    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(0x01, alg);
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, alg);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_TRUE(uECC_verify(pub, digest, 32u, sig, uECC_secp256r1()));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_tier_is_software);
    RUN_TEST(test_the_only_algorithm_is_es256);
    RUN_TEST(test_random_is_refused_and_the_buffer_untouched);
    RUN_TEST(test_the_public_key_is_the_uds_deviceid_point);
    RUN_TEST(test_no_uds_has_no_public_key_and_the_buffer_untouched);
    RUN_TEST(test_there_is_no_counter_and_the_value_untouched);
    RUN_TEST(test_operator_verify_is_refused_without_reading_the_key);
    RUN_TEST(test_image_verify_is_refused_without_reading_its_arguments);
    RUN_TEST(test_hmac_refuses_any_slot_but_zero);
    RUN_TEST(test_hmac_checks_the_slot_before_provisioning);
    RUN_TEST(test_hmac_without_a_uds_is_not_provisioned);
    RUN_TEST(test_sign_without_a_uds_is_not_provisioned_and_writes_nothing);
    RUN_TEST(test_sha256_of_abc);
    RUN_TEST(test_sha256_needs_no_provisioning);
    RUN_TEST(test_no_handoff_derives_the_cdi_from_the_local_uds);
    RUN_TEST(test_a_second_init_without_a_uds_unprovisions);
    RUN_TEST(test_a_software_handoff_cdi_keys_the_hmac);
    RUN_TEST(test_a_mismatched_tier_handoff_refuses_even_with_a_local_uds);
    RUN_TEST(test_a_mismatched_tier_cdi_is_drained);
    RUN_TEST(test_a_software_handoff_with_the_zero_sentinel_refuses_even_with_a_local_uds);
    RUN_TEST(test_sign_is_es256_and_verifies_under_the_public_key);
    return UNITY_END();
}
