/*
 * The ATTEST frame's AlgID names the root that signed it (issue #130).
 *
 * An auto image runs the element when one answers at init, else the soft
 * root, and on the Mega 2560 both sign ES256. The frame tells them apart by
 * its AlgID: 0x01 from the element, 0x11 from the soft root, and 0x10 from
 * the soft root's HMAC identity. The provider is the suite's own stand-in,
 * substituted at link time, so one link runs every tier and algorithm. The
 * expected bytes are wire literals, not the macros, so a renumbering fails
 * here.
 */
#include <string.h>
#include "unity.h"
#include "attest.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"
#include "tfw/types.h"
#include "../../src/core/app_runner.h"
#include "../../src/core/handoff.h"
#include "../../src/core/platform_id.h"
#include "../../src/crypto/sha256.h"

static const uint8_t ID[TFW_ATTEST_ID_LEN] = { 0x11u };
static const uint8_t FW[TFW_SHA256_LEN]    = { 0x22u };
static const uint8_t NONCE[TFW_ATTEST_NONCE_LEN] = {
    0x30u, 0x31u, 0x32u, 0x33u, 0x34u, 0x35u, 0x36u, 0x37u,
    0x38u, 0x39u, 0x3Au, 0x3Bu, 0x3Cu, 0x3Du, 0x3Eu, 0x3Fu,
};

static tfw_crypto_tier_t tier;
static uint8_t           sign_alg;
static uint8_t           signed_digest[TFW_SHA256_LEN];

const uint8_t *tfw_platform_id_serial(void)       { return ID; }
const uint8_t *tfw_boot_handoff_fw_hash(void)     { return FW; }
const uint8_t *tfw_boot_handoff_measurement(void) { return FW; }
tfw_status_t tfw_app_runner_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}
tfw_ms_t tfw_hal_millis(void) { return 0x01020304u; }

tfw_crypto_tier_t tfw_crypto_tier(void) { return tier; }

tfw_status_t tfw_crypto_counter(uint8_t idx, uint32_t *out)
{
    (void)idx;
    (void)out;
    return TFW_ERR_CRYPTO;
}

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    tfw_sha256(data, len, out);
    return TFW_OK;
}

tfw_status_t tfw_crypto_sign(const uint8_t digest[TFW_SHA256_LEN],
                             uint8_t sig[TFW_SIG_LEN], uint8_t *alg_id)
{
    memcpy(signed_digest, digest, TFW_SHA256_LEN);
    memset(sig, 0x55u, TFW_SIG_LEN);
    *alg_id = sign_alg;
    return TFW_OK;
}

void setUp(void)
{
    tier     = TFW_CRYPTO_SOFTWARE;
    sign_alg = TFW_ALG_ECDSA_P256_SHA256;
    memset(signed_digest, 0, sizeof signed_digest);
    tfw_attest_reset_state();
}

void tearDown(void) {}

/* ATTEST(NONCE), page 0; returns the frame's AlgID byte (offset 73). */
static uint8_t alg_of_a_fresh_frame(void)
{
    static uint8_t in[TFW_ATTEST_NONCE_LEN];
    static uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t        page = 1u;
    tfw_reader_t   r;
    tfw_writer_t   w;

    memcpy(in, NONCE, sizeof in);
    tfw_reader_init(&r, in, sizeof in);
    tfw_writer_init(&w, out, TFW_MAX_PAYLOAD);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_attest_cmd(&r, &w));

    /* Page 1 starts at offset 48, so the AlgID is its byte 25. */
    tfw_reader_init(&r, &page, 1u);
    tfw_writer_init(&w, out, TFW_MAX_PAYLOAD);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_attest_cmd(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(1u, out[0]);
    return out[2u + (73u - TFW_ATTEST_PAGE)];
}

static void test_the_soft_root_s_es256_frame_says_0x11(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x11u, alg_of_a_fresh_frame());
}

static void test_the_element_s_es256_frame_says_0x01(void)
{
    tier = TFW_CRYPTO_SECURE_ELEMENT;
    TEST_ASSERT_EQUAL_HEX8(0x01u, alg_of_a_fresh_frame());
}

static void test_the_soft_root_s_hmac_frame_says_0x10(void)
{
    sign_alg = TFW_ALG_HMAC_SHA256;
    TEST_ASSERT_EQUAL_HEX8(0x10u, alg_of_a_fresh_frame());
}

static void test_the_element_s_hmac_frame_is_not_relabelled(void)
{
    /* Only the soft root's ES256 is renamed; nothing else the provider
     * reports is changed. */
    tier     = TFW_CRYPTO_SECURE_ELEMENT;
    sign_alg = TFW_ALG_HMAC_SHA256;
    TEST_ASSERT_EQUAL_HEX8(0x10u, alg_of_a_fresh_frame());
}

/* SHA-256 of the 73 bytes before the AlgID: version 1, NONCE, ID, FW, a zero
 * boot count and the uptime 0x01020304 little-endian. Computed with Python's
 * hashlib from the layout in docs/dice-attestation/api.md, not by this code. */
static const uint8_t SIGNED_SPAN_DIGEST[TFW_SHA256_LEN] = {
    0xB5u, 0x9Du, 0xA5u, 0x58u, 0x03u, 0xABu, 0x44u, 0xB0u,
    0x6Du, 0x7Eu, 0xEEu, 0x1Du, 0x8Au, 0x6Fu, 0x6Eu, 0x5Au,
    0xE2u, 0x1Du, 0x52u, 0x11u, 0x6Eu, 0x9Au, 0xCBu, 0x1Au,
    0x09u, 0x69u, 0x43u, 0x5Du, 0xC0u, 0x2Eu, 0xDAu, 0x29u,
};

static void test_the_signed_span_is_the_same_under_either_root(void)
{
    /* The AlgID is outside the signed span (api.md), so naming the root
     * moves no signed byte: both roots sign one digest for one frame. */
    (void)alg_of_a_fresh_frame();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SIGNED_SPAN_DIGEST, signed_digest, TFW_SHA256_LEN);
    tier = TFW_CRYPTO_SECURE_ELEMENT;
    (void)alg_of_a_fresh_frame();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SIGNED_SPAN_DIGEST, signed_digest, TFW_SHA256_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_soft_root_s_es256_frame_says_0x11);
    RUN_TEST(test_the_element_s_es256_frame_says_0x01);
    RUN_TEST(test_the_soft_root_s_hmac_frame_says_0x10);
    RUN_TEST(test_the_element_s_hmac_frame_is_not_relabelled);
    RUN_TEST(test_the_signed_span_is_the_same_under_either_root);
    return UNITY_END();
}
