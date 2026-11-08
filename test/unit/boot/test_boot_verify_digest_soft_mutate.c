/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Layer 0's digest verify on the p256-soft row (boot/hal/esp32/verify_digest_soft.c)
 * under seeded mutation (#163): every key byte reaches the check, a digest is
 * taken whole, only micro-ecc's 1 is a pass, and an erased slot is a refusal
 * with TFW_ERR_CRYPTO, never a pass and never another status.
 *
 * The key is the image key test_boot_l0_esp32.c already carries; its public
 * half is computed here, so no key pair is stored.
 *
 * Named equivalent: dropping the erased-slot check. An all-0xFF X || Y is not
 * a point on P-256, so micro-ecc refuses it with the same TFW_ERR_CRYPTO.
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "sha256.h"
#include "tfw/crypto.h"
#include "uECC.h"

uint8_t tfw_boot_p256_key[64];

/* Throwaway keys, made by micro-ecc on first use (no private key is in the
 * tree) and kept for the run, so every case signs with the same pair. */
static uint8_t image_key[32];

static void make_keys(void)
{
    static int made;
    uint8_t    pub[64];

    if (!made) {
        TEST_ASSERT_EQUAL_INT(1, uECC_make_key(pub, image_key, uECC_secp256r1()));
        made = 1;
    }
}

typedef struct { uECC_HashContext uecc; tfw_sha256_t ctx; } hash_ctx_t;
static hash_ctx_t *as_ctx(const uECC_HashContext *c) { return (hash_ctx_t *)(uintptr_t)c; }
static void h_init(const uECC_HashContext *c) { tfw_sha256_init(&as_ctx(c)->ctx); }
static void h_update(const uECC_HashContext *c, const uint8_t *m, unsigned n)
{
    tfw_sha256_update(&as_ctx(c)->ctx, m, (uint16_t)n);
}
static void h_finish(const uECC_HashContext *c, uint8_t *out)
{
    tfw_sha256_final(&as_ctx(c)->ctx, out);
}

static void sign(const uint8_t key[32], const uint8_t h[32], uint8_t sig[64])
{
    uint8_t    tmp[32 + 32 + 64];
    hash_ctx_t hc;

    memset(&hc, 0, sizeof hc);
    hc.uecc.init_hash   = h_init;
    hc.uecc.update_hash = h_update;
    hc.uecc.finish_hash = h_finish;
    hc.uecc.block_size  = 64u;
    hc.uecc.result_size = 32u;
    hc.uecc.tmp         = tmp;
    TEST_ASSERT_EQUAL_INT(1, uECC_sign_deterministic(key, h, 32u, &hc.uecc, sig,
                                                     uECC_secp256r1()));
}

static uint8_t digest[32];
static uint8_t sig[64];

void setUp(void)
{
    static const uint8_t msg[] = "layer 0 digest verify";

    make_keys();
    TEST_ASSERT_EQUAL_INT(1, uECC_compute_public_key(image_key, tfw_boot_p256_key,
                                                     uECC_secp256r1()));
    tfw_sha256(msg, (uint16_t)(sizeof msg - 1u), digest);
    sign(image_key, digest, sig);
}
void tearDown(void) {}

static void test_a_good_signature_passes(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_verify_image(digest, sig));
}

static void test_a_flipped_signature_bit_is_refused(void)
{
    sig[63] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_verify_image(digest, sig));
}

/* The last digest byte reaches the check: a digest taken one byte short
 * would still pass here. */
static void test_the_last_digest_byte_is_verified(void)
{
    digest[31] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_verify_image(digest, sig));
}

/* Each half of the key: the first and the last byte of Y are read. */
static void test_the_last_key_byte_is_verified(void)
{
    tfw_boot_p256_key[63] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_verify_image(digest, sig));
}

static void test_another_key_refuses(void)
{
    uint8_t other_key[32];

    TEST_ASSERT_EQUAL_INT(1, uECC_make_key(tfw_boot_p256_key, other_key, uECC_secp256r1()));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_verify_image(digest, sig));
}

static void test_an_erased_slot_is_refused_with_crypto(void)
{
    memset(tfw_boot_p256_key, 0xFF, sizeof tfw_boot_p256_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_verify_image(digest, sig));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_good_signature_passes);
    RUN_TEST(test_a_flipped_signature_bit_is_refused);
    RUN_TEST(test_the_last_digest_byte_is_verified);
    RUN_TEST(test_the_last_key_byte_is_verified);
    RUN_TEST(test_another_key_refuses);
    RUN_TEST(test_an_erased_slot_is_refused_with_crypto);
    return UNITY_END();
}
