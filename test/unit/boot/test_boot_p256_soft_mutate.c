/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's soft ES256 verify, boot/verify/verify_p256_soft.c, against seeded
 * mutants (#163). The key slot is writable here, as in test_boot_p256_soft.
 * A key pair is made fresh by micro-ecc in the test (no private key is in the
 * tree), so a signature exists over any message the test needs: a bit flipped
 * in each byte of the signature, the message or the slot must refuse, and so
 * must a message one byte short or long, another key's signature, a supplied
 * key, a NULL message whose empty-message signature is valid, and an erased
 * slot. Wycheproof's first valid vector of key 0 anchors the result to a
 * signature this code did not make.
 *
 * Mutants tried, each killed by the test named:
 *   `pub != 0` refusal dropped / inverted       -> a_supplied_key_is_refused_even_when_right
 *   `msg == 0` refusal dropped                  -> a_null_message_is_refused
 *   `sig == 0` refusal dropped                  -> a_null_signature_is_refused (faults)
 *   slot read bound 64 -> 63, index i -> i + 1  -> a_fresh_key_verifies_its_own_signature,
 *                                                  every_slot_byte_matters
 *   `erased &= key` -> `|=`, `== 0xFF` -> `!=`   -> a_fresh_key_verifies_its_own_signature
 *   hash over msglen - 1, digest 31 bytes to uECC
 *                                               -> every_message_byte_matters,
 *                                                  the_wycheproof_anchor_verifies
 *   `ok == 1` -> `ok == 0`, always TFW_OK        -> every_signature_byte_matters
 *   `pub` refusal returns CRYPTO               -> a_supplied_key_is_refused_even_when_right
 *   `msg == 0 || sig == 0` -> `&&`             -> a_null_signature_is_refused
 *   hash over a fixed 100 bytes                -> every_message_byte_matters
 *   tfw_boot_pubkey writes, or returns TFW_OK  -> the_contract
 *   tfw_boot_cose_alg 0x26 -> 0x27              -> the_contract
 * Survive, and equivalent from outside (24 tried, 22 killed with the slot's):
 *   dropping the erased-slot refusal (verify_p256_soft.c:59): micro-ecc refuses
 *   the all-0xFF point by itself; the line stays as the refusal that does not
 *   depend on a library's handling of an off-curve key.
 *   `ok == 1` -> `ok != 0` (verify_p256_soft.c:66): uECC_verify returns 0 or 1.
 *
 * xorshift32 with a literal seed feeds micro-ecc's RNG: a failure reproduces.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "boot_verify.h"
#include "sha256.h"
#include "uECC.h"
#include "wycheproof_p256.h"

uint8_t tfw_boot_p256_key[64];

extern const uint8_t tfw_boot_cose_alg;

#define MSG_LEN 100u

static uint32_t rng_state;
static uint8_t  priv[32], pub[64];
static uint8_t  msg[MSG_LEN];
static uint8_t  sig[64];

static int test_rng(uint8_t *dest, unsigned size)
{
    while (size--) {
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 17;
        rng_state ^= rng_state << 5;
        *dest++ = (uint8_t)rng_state;
    }
    return 1;
}

static void sign(const uint8_t *m, uint16_t len, const uint8_t *key, uint8_t out[64])
{
    uint8_t digest[32];

    tfw_sha256(m, len, digest);
    TEST_ASSERT_EQUAL_INT(1, uECC_sign(key, digest, 32u, out, uECC_secp256r1()));
}

void setUp(void)
{
    rng_state = 0x0E5256A1u;
    uECC_set_rng(test_rng);
    TEST_ASSERT_EQUAL_INT(1, uECC_make_key(pub, priv, uECC_secp256r1()));
    memcpy(tfw_boot_p256_key, pub, 64);
    for (uint16_t i = 0u; i < MSG_LEN; i++) { msg[i] = (uint8_t)(i * 7u + 1u); }
    sign(msg, MSG_LEN, priv, sig);
}

void tearDown(void) {}

static void test_a_fresh_key_verifies_its_own_signature(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(msg, MSG_LEN, sig, 0));
}

static void test_the_wycheproof_anchor_verifies(void)
{
    uint16_t v;

    for (v = 0u; v < WYC_NVECS; v++) {
        if (wyc_vecs[v].key == 0u && wyc_vecs[v].valid) { break; }
    }
    TEST_ASSERT_LESS_THAN_UINT16(WYC_NVECS, v);
    memcpy(tfw_boot_p256_key, wyc_keys[0], 64);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off], wyc_vecs[v].len,
                                                 wyc_vecs[v].sig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off],
                                                         (uint16_t)(wyc_vecs[v].len + 1u),
                                                         wyc_vecs[v].sig, 0));
}

static void test_every_signature_byte_matters(void)
{
    for (uint8_t i = 0u; i < 64u; i++) {
        sig[i] ^= (uint8_t)(1u << (i & 7u));
        TEST_ASSERT_EQUAL_INT_MESSAGE(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, sig, 0), "sig byte");
        sig[i] ^= (uint8_t)(1u << (i & 7u));
    }
    memset(sig, 0, sizeof sig);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, sig, 0));
    memset(sig, 0xFF, sizeof sig);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, sig, 0));
}

static void test_every_message_byte_matters(void)
{
    for (uint16_t i = 0u; i < MSG_LEN; i++) {
        msg[i] ^= (uint8_t)(0x80u >> (i & 7u));
        TEST_ASSERT_EQUAL_INT_MESSAGE(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, sig, 0), "msg byte");
        msg[i] ^= (uint8_t)(0x80u >> (i & 7u));
    }
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN - 1u, sig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, 0u, sig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(msg, MSG_LEN, sig, 0));
}

static void test_every_slot_byte_matters(void)
{
    for (uint8_t i = 0u; i < 64u; i++) {
        tfw_boot_p256_key[i] ^= (uint8_t)(1u << (i & 7u));
        TEST_ASSERT_EQUAL_INT_MESSAGE(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, sig, 0), "slot byte");
        tfw_boot_p256_key[i] ^= (uint8_t)(1u << (i & 7u));
    }
}

static void test_another_keys_signature_is_refused(void)
{
    uint8_t priv2[32], pub2[64], sig2[64];

    TEST_ASSERT_EQUAL_INT(1, uECC_make_key(pub2, priv2, uECC_secp256r1()));
    sign(msg, MSG_LEN, priv2, sig2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, sig2, 0));
    memcpy(tfw_boot_p256_key, pub2, 64);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(msg, MSG_LEN, sig2, 0));
}

static void test_a_supplied_key_is_refused_even_when_right(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_verify(msg, MSG_LEN, sig, pub));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_verify(0, 0u, 0, pub));
}

/* A signature over the empty message is valid for (msg, 0) and refused for
 * (NULL, 0): the NULL is refused, not hashed as nothing. */
static void test_a_null_message_is_refused(void)
{
    uint8_t esig[64];

    sign(msg, 0u, priv, esig);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(msg, 0u, esig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(0, 0u, esig, 0));
}

static void test_a_null_signature_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, 0, 0));
}

static void test_an_erased_slot_refuses(void)
{
    memset(tfw_boot_p256_key, 0xFF, 64);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, MSG_LEN, sig, 0));
}

static void test_the_contract(void)
{
    uint8_t out[TFW_BOOT_PUBKEY_LEN];

    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_pubkey(out));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0x26u, tfw_boot_cose_alg);   /* ES256, RFC 9053 -7 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_key_verifies_its_own_signature);
    RUN_TEST(test_the_wycheproof_anchor_verifies);
    RUN_TEST(test_every_signature_byte_matters);
    RUN_TEST(test_every_message_byte_matters);
    RUN_TEST(test_every_slot_byte_matters);
    RUN_TEST(test_another_keys_signature_is_refused);
    RUN_TEST(test_a_supplied_key_is_refused_even_when_right);
    RUN_TEST(test_a_null_message_is_refused);
    RUN_TEST(test_a_null_signature_is_refused);
    RUN_TEST(test_an_erased_slot_refuses);
    RUN_TEST(test_the_contract);
    return UNITY_END();
}
