/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The unattested tier: every operation refuses, and none of them
 * approximates -- see the file header of src/crypto/none/provider.c for why
 * this tier exists at all.
 */

#include <string.h>
#include "unity.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "hal_mock.h"
#include "handoff.h"
#include "boot_measure.h"

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) {}

static void test_reports_the_unattested_tier(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_NONE, tfw_crypto_tier());
}

static void test_the_algorithm_bitmap_is_empty(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_crypto_algorithms());
}

/*
 * A CDI tfw-boot handed off must not sit resident in src/core/handoff.c for
 * the process lifetime, even on a tier that signs with nothing and derives
 * nothing itself -- the "consumed once" contract include/tfw/boot.h describes
 * applies to every provider, not only the two that have a use for the value.
 * Provable from outside that module: tfw_boot_handoff_take_cdi returns 0
 * (nothing left to take) after tfw_crypto_init has run.
 */
static void test_init_drains_a_handed_off_cdi_even_though_this_tier_signs_with_nothing(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], measurement[32], taken[32];
    uint8_t i;

    for (i = 0u; i < sizeof cdi; i++)         { cdi[i] = (uint8_t)(0x33u + i); }
    for (i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, measurement, 1u, 0u);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_take_cdi(taken));
}

/* A refusal writes nothing: a caller that ignores the status must not find a
 * plausible nonce, digest, MAC, signature, key or counter in its buffer. */
static void test_random_refuses_and_leaves_the_buffer_untouched(void)
{
    uint8_t out[16];
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_random(out, (uint8_t)sizeof out));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xA5u, out, sizeof out);
}

static void test_sha256_refuses_and_leaves_the_digest_untouched(void)
{
    uint8_t data[16] = {0};
    uint8_t out[TFW_SHA256_LEN];
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_sha256(data, (uint16_t)sizeof data, out));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xA5u, out, sizeof out);
}

static void test_hmac_refuses_and_leaves_the_mac_untouched(void)
{
    uint8_t data[16] = {0};
    uint8_t out[TFW_SHA256_LEN];
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_hmac(0u, data, (uint16_t)sizeof data, out));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xA5u, out, sizeof out);
}

static void test_sign_refuses_and_leaves_signature_and_alg_untouched(void)
{
    uint8_t digest[TFW_SHA256_LEN] = {0};
    uint8_t sig[TFW_SIG_LEN];
    uint8_t alg = 0xA5u;
    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xA5u, sig, sizeof sig);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, alg);
}

static void test_pubkey_refuses_and_leaves_the_key_untouched(void)
{
    uint8_t out[TFW_SIG_LEN];
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_pubkey(out));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xA5u, out, sizeof out);
}

static void test_counter_refuses_and_leaves_the_count_untouched(void)
{
    uint32_t n = 0xA5A5A5A5u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_counter(0u, &n));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, n);
}

/* An all-zero signature is the cheapest forgery; a tier that verifies nothing
 * must refuse it, not pass it. */
static void test_verify_operator_refuses_even_an_all_zero_signature(void)
{
    uint8_t digest[TFW_SHA256_LEN] = {0}, sig[TFW_SIG_LEN] = {0}, pub[TFW_SIG_LEN] = {0};
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_operator(digest, sig, pub));
}

static void test_verify_image_refuses_even_an_all_zero_signature(void)
{
    uint8_t digest[TFW_SHA256_LEN] = {0}, sig[TFW_SIG_LEN] = {0};
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_image(digest, sig));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reports_the_unattested_tier);
    RUN_TEST(test_the_algorithm_bitmap_is_empty);
    RUN_TEST(test_init_drains_a_handed_off_cdi_even_though_this_tier_signs_with_nothing);
    RUN_TEST(test_random_refuses_and_leaves_the_buffer_untouched);
    RUN_TEST(test_sha256_refuses_and_leaves_the_digest_untouched);
    RUN_TEST(test_hmac_refuses_and_leaves_the_mac_untouched);
    RUN_TEST(test_sign_refuses_and_leaves_signature_and_alg_untouched);
    RUN_TEST(test_pubkey_refuses_and_leaves_the_key_untouched);
    RUN_TEST(test_counter_refuses_and_leaves_the_count_untouched);
    RUN_TEST(test_verify_operator_refuses_even_an_all_zero_signature);
    RUN_TEST(test_verify_image_refuses_even_an_all_zero_signature);
    return UNITY_END();
}
