/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * BOOT_SIG=p256-soft (#130): the vendored micro-ecc against every Wycheproof
 * P-256 / SHA-256 P1363 vector, and the shipping backend against the same key
 * slot shape and contract the element tier honours.
 *
 * Then the backend, boot/verify/verify_p256_soft.c, against seeded mutants
 * (#163). A key pair is made fresh by micro-ecc in setUp (no private key is in
 * the tree), so a signature exists over any message the test needs: a bit
 * flipped in each byte of the signature, the message or the slot must refuse,
 * and so must a message one byte short or long, another key's signature, a
 * supplied key, a NULL message whose empty-message signature is valid, and an
 * erased slot. Wycheproof's first valid vector of key 0 anchors the result to
 * a signature this code did not make.
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
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_verify.h"
#include "sha256.h"
#include "uECC.h"
#include "wycheproof_p256.h"

/* The host test's key slot, in place of p256_key_slot.c. Writable here so each
 * test installs the key it needs; the backend only ever reads it. */
uint8_t tfw_boot_p256_key[64];

#define MSG_LEN 100u

static uint32_t rng_state;
static uint8_t  k_priv[32], k_pub[64];
static uint8_t  k_msg[MSG_LEN];
static uint8_t  k_sig[64];

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
    TEST_ASSERT_EQUAL_INT(1, uECC_make_key(k_pub, k_priv, uECC_secp256r1()));
    memcpy(tfw_boot_p256_key, k_pub, 64);
    for (uint16_t i = 0u; i < MSG_LEN; i++) { k_msg[i] = (uint8_t)(i * 7u + 1u); }
    sign(k_msg, MSG_LEN, k_priv, k_sig);
}

void tearDown(void) {}

static void test_every_wycheproof_vector_gets_its_verdict(void)
{
    uint8_t  digest[32];
    uint16_t i;
    uint16_t valid = 0u;
    uint16_t wrong = 0u;

    /* Every vector runs; each wrong verdict is printed, not just the first. */
    for (i = 0u; i < WYC_NVECS; i++) {
        tfw_sha256(&wyc_msgs[wyc_vecs[i].off], wyc_vecs[i].len, digest);
        int ok = uECC_verify(wyc_keys[wyc_vecs[i].key], digest, 32u,
                             wyc_vecs[i].sig, uECC_secp256r1());
        if (ok != (int)wyc_vecs[i].valid) {
            printf("WRONG tcId %u, expected %u, got %d, %s\n",
                   (unsigned)wyc_vecs[i].tcid, (unsigned)wyc_vecs[i].valid, ok,
                   wyc_vecs[i].why);
            wrong++;
        }
        valid = (uint16_t)(valid + wyc_vecs[i].valid);
    }
    TEST_ASSERT_EQUAL_UINT16(0u, wrong);
    /* 241 vectors: 173 accepted, 68 refused. The 21 with a signature that is
     * not 64 bytes cannot reach the 64-byte API, so they are refused by type. */
    TEST_ASSERT_EQUAL_UINT16(241u, WYC_NVECS);
    TEST_ASSERT_EQUAL_UINT16(173u, valid);
}

static uint16_t first_valid_of_key0(void)
{
    uint16_t i;

    for (i = 0u; i < WYC_NVECS; i++) {
        if (wyc_vecs[i].key == 0u && wyc_vecs[i].valid) {
            return i;
        }
    }
    TEST_FAIL_MESSAGE("no valid vector for key 0");
    return 0u;
}

static void test_a_fresh_key_verifies_its_own_signature(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(k_msg, MSG_LEN, k_sig, 0));
}

static void test_the_wycheproof_anchor_verifies(void)
{
    uint16_t v = first_valid_of_key0();

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
        k_sig[i] ^= (uint8_t)(1u << (i & 7u));
        TEST_ASSERT_EQUAL_INT_MESSAGE(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN, k_sig, 0),
                                      "sig byte");
        k_sig[i] ^= (uint8_t)(1u << (i & 7u));
    }
    memset(k_sig, 0, sizeof k_sig);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN, k_sig, 0));
    memset(k_sig, 0xFF, sizeof k_sig);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN, k_sig, 0));
}

static void test_every_message_byte_matters(void)
{
    for (uint16_t i = 0u; i < MSG_LEN; i++) {
        k_msg[i] ^= (uint8_t)(0x80u >> (i & 7u));
        TEST_ASSERT_EQUAL_INT_MESSAGE(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN, k_sig, 0),
                                      "msg byte");
        k_msg[i] ^= (uint8_t)(0x80u >> (i & 7u));
    }
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN - 1u, k_sig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, 0u, k_sig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(k_msg, MSG_LEN, k_sig, 0));
}

static void test_every_slot_byte_matters(void)
{
    for (uint8_t i = 0u; i < 64u; i++) {
        tfw_boot_p256_key[i] ^= (uint8_t)(1u << (i & 7u));
        TEST_ASSERT_EQUAL_INT_MESSAGE(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN, k_sig, 0),
                                      "slot byte");
        tfw_boot_p256_key[i] ^= (uint8_t)(1u << (i & 7u));
    }
}

static void test_another_keys_signature_is_refused(void)
{
    uint8_t priv2[32], pub2[64], sig2[64];

    TEST_ASSERT_EQUAL_INT(1, uECC_make_key(pub2, priv2, uECC_secp256r1()));
    sign(k_msg, MSG_LEN, priv2, sig2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN, sig2, 0));
    memcpy(tfw_boot_p256_key, pub2, 64);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(k_msg, MSG_LEN, sig2, 0));
}

/* The element tier's contract: a supplied key is refused, even the right one. */
static void test_a_supplied_key_is_refused_even_when_right(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_verify(k_msg, MSG_LEN, k_sig, k_pub));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_verify(0, 0u, 0, k_pub));
}

/* A NULL message is refused even where its empty-message signature is valid. */
static void test_a_null_message_is_refused(void)
{
    uint8_t esig[64];

    sign(k_msg, 0u, k_priv, esig);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(k_msg, 0u, esig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(0, 0u, esig, 0));
}

static void test_a_null_signature_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN, 0, 0));
}

static void test_an_erased_slot_refuses(void)
{
    memset(tfw_boot_p256_key, 0xFF, 64);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(k_msg, MSG_LEN, k_sig, 0));
}

/* As the element: no key to read out, the buffer left alone, and ES256. */
static void test_the_contract(void)
{
    uint8_t out[TFW_BOOT_PUBKEY_LEN];

    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_pubkey(out));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof out);
    TEST_ASSERT_EQUAL_HEX8(0x26u, tfw_boot_cose_alg);   /* ES256, RFC 9053 -7 */
}

/* The element tier's signing key: boot_harness.c's EL_SIGNING_KEY, the key the
 * simavr element tier signs its trailers with. Signed here at test time, as the
 * model signs (test/model/atecc608a.c, tfw_model_p256_sign); nothing is pinned. */
static const uint8_t el_signing_key[32] = {
    0x9E,0x1C,0x4B,0x22,0x70,0xA5,0x33,0xF8,0x0D,0x61,0xB4,0x17,0xCA,0x2E,0x88,0x5D,
    0x30,0x79,0xE6,0x44,0x12,0xBB,0x57,0x9A,0xC3,0x08,0x6F,0xD1,0x25,0xAE,0x73,0x40
};

/* micro-ecc's deterministic k takes the hash as a context: the model's glue. */
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

/* A trailer v1 signed as the element tier signs it: P-256 over tfw_boot_hash of
 * the signed span, r || s at TFW_BOOT_TR_SIGNATURE. Its public key goes in the
 * slot. */
static void element_signed_trailer(uint8_t raw[TFW_BOOT_TRAILER_LEN])
{
    tfw_boot_trailer_t tr;
    uint8_t            digest[32];
    uint8_t            tmp[32 + 32 + 64];
    hash_ctx_t         hc;
    uint8_t            i;

    memset(&tr, 0, sizeof tr);
    tr.image_version = 7u;
    tr.image_len     = 1024u;
    tr.board_id      = 2u;
    for (i = 0u; i < 32u; i++) {
        tr.measurement[i] = (uint8_t)(0x30u + i);
    }
    tfw_boot_trailer_serialise(&tr, raw);
    tfw_boot_hash(raw, TFW_BOOT_TR_SIGNED_LEN, digest);

    memset(&hc, 0, sizeof hc);
    hc.uecc.init_hash   = h_init;
    hc.uecc.update_hash = h_update;
    hc.uecc.finish_hash = h_finish;
    hc.uecc.block_size  = 64u;
    hc.uecc.result_size = 32u;
    hc.uecc.tmp         = tmp;
    TEST_ASSERT_EQUAL_INT(1, uECC_sign_deterministic(el_signing_key, digest, 32u, &hc.uecc,
                                                     &raw[TFW_BOOT_TR_SIGNATURE],
                                                     uECC_secp256r1()));
    TEST_ASSERT_EQUAL_INT(1, uECC_compute_public_key(el_signing_key,
                                                     (uint8_t *)tfw_boot_p256_key,
                                                     uECC_secp256r1()));
}

static tfw_status_t verify_trailer(const uint8_t raw[TFW_BOOT_TRAILER_LEN])
{
    return tfw_boot_verify(raw, TFW_BOOT_TR_SIGNED_LEN, &raw[TFW_BOOT_TR_SIGNATURE], 0);
}

static void test_an_element_tier_trailer_verifies_under_p256_soft(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];

    element_signed_trailer(raw);
    TEST_ASSERT_EQUAL_INT(TFW_OK, verify_trailer(raw));

    raw[TFW_BOOT_TR_SIGNATURE + 17u] ^= 0x04u;   /* one bit of r */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, verify_trailer(raw));
    raw[TFW_BOOT_TR_SIGNATURE + 17u] ^= 0x04u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, verify_trailer(raw));

    raw[TFW_BOOT_TR_SIGNED_LEN - 1u] ^= 0x80u;   /* one bit of the covered span */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, verify_trailer(raw));
    raw[TFW_BOOT_TR_SIGNED_LEN - 1u] ^= 0x80u;

    memset((void *)tfw_boot_p256_key, 0xFF, 64);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, verify_trailer(raw));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_wycheproof_vector_gets_its_verdict);
    RUN_TEST(test_an_element_tier_trailer_verifies_under_p256_soft);
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
