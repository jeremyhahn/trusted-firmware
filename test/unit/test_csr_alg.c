/*
 * ATTEST_CSR against a stand-in provider, substituted at link time.
 *
 * No provider this project ships hands out a public key and then signs under
 * anything but ES256: the element signs P-256, the soft and none tiers have no
 * public key to give. tfw_csr_cmd still refuses the mismatch, because the host
 * names ecdsa-with-SHA256 in the CertificationRequest it wraps the signature
 * in. This link is the one place that refusal can run.
 */
#include <string.h>
#include "unity.h"
#include "csr.h"
#include "attest.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/types.h"

static const uint8_t ID[TFW_ATTEST_ID_LEN]   = { 0x11u };
static const uint8_t FW[TFW_SHA256_LEN]      = { 0x22u };
static const uint8_t NONCE[TFW_ALIAS_NONCE_LEN] = { 0x33u };

static tfw_status_t pubkey_st;
static uint8_t      sign_alg;
static uint8_t      signs;

const uint8_t *tfw_attest_device_id(void) { return ID; }
const uint8_t *tfw_attest_fw_hash(void)   { return FW; }

tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    memset(out, 0x44u, 64u);
    return pubkey_st;
}

tfw_status_t tfw_crypto_sign(const uint8_t digest[TFW_SHA256_LEN],
                             uint8_t sig[TFW_SIG_LEN], uint8_t *alg_id)
{
    (void)digest;
    signs++;
    memset(sig, 0x55u, TFW_SIG_LEN);
    *alg_id = sign_alg;
    return TFW_OK;
}

void setUp(void)
{
    pubkey_st = TFW_OK;
    sign_alg  = TFW_ALG_ECDSA_P256_SHA256;
    signs     = 0u;
}

void tearDown(void) {}

static tfw_status_t run(uint8_t *len)
{
    static uint8_t in[TFW_ALIAS_NONCE_LEN];
    static uint8_t out[TFW_MAX_PAYLOAD];
    tfw_reader_t   r;
    tfw_writer_t   w;
    tfw_status_t   st;

    memcpy(in, NONCE, sizeof in);
    tfw_reader_init(&r, in, sizeof in);
    tfw_writer_init(&w, out, TFW_MAX_PAYLOAD);
    st = tfw_csr_cmd(&r, &w);
    *len = tfw_writer_len(&w);
    return st;
}

static void test_an_es256_signature_is_returned(void)
{
    uint8_t len = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, run(&len));
    TEST_ASSERT_EQUAL_UINT8(TFW_SIG_LEN, len);
}

static void test_a_signature_under_another_algorithm_is_refused_and_writes_nothing(void)
{
    uint8_t len = 0xFFu;

    sign_alg = TFW_ALG_HMAC_SHA256;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run(&len));
    TEST_ASSERT_EQUAL_UINT8(1u, signs);
    TEST_ASSERT_EQUAL_UINT8(0u, len);
}

static void test_a_tier_with_no_public_key_refuses_before_signing(void)
{
    uint8_t len = 0xFFu;

    pubkey_st = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run(&len));
    TEST_ASSERT_EQUAL_UINT8(0u, signs);
    TEST_ASSERT_EQUAL_UINT8(0u, len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_es256_signature_is_returned);
    RUN_TEST(test_a_signature_under_another_algorithm_is_refused_and_writes_nothing);
    RUN_TEST(test_a_tier_with_no_public_key_refuses_before_signing);
    return UNITY_END();
}
