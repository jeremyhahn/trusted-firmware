/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * BOOT_SIG=p256-soft (#130): the vendored micro-ecc against every Wycheproof
 * P-256 / SHA-256 P1363 vector, and the shipping backend against the same key
 * slot shape and contract the element tier honours.
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

void setUp(void) {}
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

static void test_the_backend_verifies_with_its_own_slot(void)
{
    uint16_t v = first_valid_of_key0();
    uint8_t  sig[64];

    memcpy((void *)tfw_boot_p256_key, wyc_keys[0], 64);
    memcpy(sig, wyc_vecs[v].sig, 64);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off],
                                                 wyc_vecs[v].len, sig, 0));
    sig[63] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off],
                                                         wyc_vecs[v].len, sig, 0));
}

static void test_an_erased_slot_refuses_every_image(void)
{
    uint16_t v = first_valid_of_key0();

    memset((void *)tfw_boot_p256_key, 0xFF, 64);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off],
                                                         wyc_vecs[v].len,
                                                         wyc_vecs[v].sig, 0));
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

static void test_the_contract_matches_the_element(void)
{
    uint8_t buf[TFW_BOOT_PUBKEY_LEN];
    uint8_t sig[TFW_BOOT_SIG_LEN];

    memset(buf, 0, sizeof buf);
    memset(sig, 0, sizeof sig);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_pubkey(buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_verify(buf, 1u, sig, buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(0, 0u, sig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(buf, 1u, 0, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_wycheproof_vector_gets_its_verdict);
    RUN_TEST(test_the_backend_verifies_with_its_own_slot);
    RUN_TEST(test_an_erased_slot_refuses_every_image);
    RUN_TEST(test_the_contract_matches_the_element);
    RUN_TEST(test_an_element_tier_trailer_verifies_under_p256_soft);
    return UNITY_END();
}
