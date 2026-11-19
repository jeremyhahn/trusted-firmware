/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure-world DICE service (#590 slice b, docs/plans/590b.md) on host.
 *
 * The reference is today's non-secure path, linked beside the service: the
 * soft root's P-256 identity (src/crypto/soft/id_p256.c) over this suite's own
 * CDI handoff, and alias.c's TBS digest and certificate signed the way its
 * mint signs them, through that identity. For the fixed UDS 00..1F every
 * service answer must be those bytes, and SEED_PUB is the point
 * test_soft_id_p256 pins for the same UDS.
 */
#include <stddef.h>
#include <string.h>
#include "unity.h"
#include "uECC.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "sha256.h"
#include "deviceid_p256.h"
#include "handoff.h"
#include "id.h"
#include "../../src/caps/alias.h"
#include "../../src/caps/attest.h"
#include "../../src/tz/dice_service.h"

static uint8_t UDS[32];
static uint8_t CDI[32];
static uint8_t UEID[TFW_ATTEST_ID_LEN];
static uint8_t FW_HASH[32];
static uint8_t NONCE[32];
static uint8_t DIGEST[32];

static const uint8_t SEED_PUB[64] = {
    0xC4,0x57,0x5D,0x64,0xA4,0xD8,0x9A,0x2A,0x53,0x2B,0x03,0x82,0xF3,0xA0,0x02,0xA2,
    0xB7,0x8D,0x4E,0xCB,0xC1,0x70,0x50,0x7E,0xDE,0x97,0x91,0xEB,0xFC,0xC9,0x7F,0x0E,
    0x55,0x4D,0xFC,0xFD,0x69,0x4E,0x65,0x91,0x04,0xB0,0xDE,0xEE,0xAC,0xCE,0x0C,0x47,
    0x00,0x2A,0x51,0x6E,0xA8,0xE5,0xF0,0xCE,0x96,0x22,0x0E,0x84,0x1C,0xC7,0x5C,0xA6
};

/* ---- the non-secure path's environment ---------------------------------- */

uint8_t tfw_boot_handoff_device_seed(uint8_t out[32])
{
    tfw_hmac_sha256(UDS, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                    TFW_DEVICEID_LABEL_LEN, out);
    return 1u;
}

/* alias.c's mint and command reach these; the provider is the soft P-256 root. */
tfw_status_t tfw_crypto_pubkey(uint8_t out[64]) { return tfw_soft_id_pubkey(out); }
tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg)
{
    return tfw_soft_id_sign(CDI, digest, sig, alg);
}
tfw_crypto_tier_t tfw_crypto_tier(void) { return TFW_CRYPTO_SOFTWARE; }
void tfw_attest_measurement(uint8_t out[TFW_SHA256_LEN]) { memcpy(out, FW_HASH, 32u); }
const uint8_t *tfw_attest_device_id(void) { return UEID; }

/* ---- helpers ------------------------------------------------------------- */

static void init_service(void)
{
    tfw_dice_inputs_t in = { UDS, CDI, UEID, FW_HASH, TFW_CRYPTO_SOFTWARE };
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_service_init(&in));
}

/* The whole alias certificate the non-secure path builds for NONCE. */
static uint16_t reference_cert(uint8_t *buf, uint16_t cap)
{
    uint8_t pub[64], sig[64], digest[32], alg = TFW_ALG_NONE, got = 0u;
    uint16_t total = 0u, off = 0u;
    tfw_alias_parts_t p = { NONCE, UEID, FW_HASH, pub, sig, TFW_CRYPTO_SOFTWARE };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    memset(sig, 0, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_alias_tbs_digest(&p, digest));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL(TFW_ALG_ECDSA_P256_SHA256, alg);
    do {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_alias_cert(&p, off, buf + off, 200u, &total, &got));
        TEST_ASSERT_TRUE(total <= cap);
        off = (uint16_t)(off + got);
    } while (off < total);
    return total;
}

/* The whole alias certificate the service serves for NONCE, page by page. */
static uint32_t service_cert(uint8_t *buf, uint32_t cap, uint32_t page)
{
    tfw_dice_cert_page_t r;
    uint32_t             off = 0u;

    do {
        memcpy(r.nonce, NONCE, sizeof r.nonce);
        r.off = off;
        r.out = buf + off;
        r.cap = page;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_cert(&r));
        TEST_ASSERT_TRUE(r.got > 0u && r.got <= page && r.total <= cap);
        off += r.got;
    } while (off < r.total);
    return r.total;
}

/* One page request with total/got preset to a marker a refusal must leave. */
static tfw_dice_cert_page_t s_page;
static tfw_status_t cert_page(uint8_t *out, uint32_t cap, uint32_t off)
{
    memcpy(s_page.nonce, NONCE, sizeof s_page.nonce);
    s_page.off   = off;
    s_page.out   = out;
    s_page.cap   = cap;
    s_page.total = 0xEEEEEEEEu;
    s_page.got   = 0xEEEEEEEEu;
    return tfw_dice_alias_cert(&s_page);
}

static int contains(const uint8_t *hay, size_t n, const uint8_t *needle)
{
    for (size_t i = 0u; i + 32u <= n; i++) {
        if (memcmp(hay + i, needle, 32u) == 0) {
            return 1;
        }
    }
    return 0;
}

void setUp(void)
{
    for (uint8_t i = 0u; i < sizeof UDS; i++) { UDS[i] = i; }
    memset(CDI, 0xD0, sizeof CDI);
    memset(UEID, 0x5A, sizeof UEID);
    memset(FW_HASH, 0x27, sizeof FW_HASH);
    for (uint8_t i = 0u; i < sizeof NONCE; i++) { NONCE[i] = (uint8_t)(0xA0u + i); }
    tfw_sha256((const uint8_t *)"dice-590b", 9u, DIGEST);
    init_service();
}

void tearDown(void)
{
    tfw_dice_service_clear();
}

/* ---- same bytes as the non-secure path ---------------------------------- */

static void test_deviceid_pub_is_the_non_secure_deviceid(void)
{
    uint8_t got[64], ref[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_deviceid_pub(got, sizeof got));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_soft_id_pubkey(ref));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEED_PUB, got, 64);
}

static void test_alias_pub_is_the_alias_certificate_subject_key(void)
{
    uint8_t got[64], ref[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_pub(got, sizeof got));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(ref));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, 64);
}

static void test_alias_cert_is_the_non_secure_certificate(void)
{
    static uint8_t ref[1024], got[1024];
    uint16_t       n = reference_cert(ref, sizeof ref);

    TEST_ASSERT_EQUAL_UINT32(n, service_cert(got, sizeof got, 255u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, n);
    memset(got, 0, sizeof got);
    TEST_ASSERT_EQUAL_UINT32(n, service_cert(got, sizeof got, 17u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, n);
}

static void test_alias_sign_is_the_non_secure_signature(void)
{
    uint8_t got[64], ref[64], alg = TFW_ALG_NONE;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_sign(DIGEST, 32u, got, sizeof got));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(DIGEST, ref, &alg));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, 64);
}

/* ---- a signature verifies under the alias key --------------------------- */

static void test_alias_sign_verifies_under_the_alias_pub(void)
{
    uint8_t pub[64], sig[64], other[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_pub(pub, sizeof pub));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_sign(DIGEST, 32u, sig, sizeof sig));
    TEST_ASSERT_TRUE(uECC_verify(pub, DIGEST, 32u, sig, uECC_secp256r1()));
    memcpy(other, DIGEST, sizeof other);
    other[0] ^= 1u;
    TEST_ASSERT_FALSE(uECC_verify(pub, other, 32u, sig, uECC_secp256r1()));
}

/* ---- NULL and short buffers: refused, nothing written -------------------- */

static void test_null_and_short_buffers_are_refused_untouched(void)
{
    uint8_t buf[300];

    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_deviceid_pub(NULL, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_deviceid_pub(buf, 63u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_deviceid_pub(buf, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_pub(NULL, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_pub(buf, 63u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_sign(NULL, 32u, buf, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_sign(DIGEST, 31u, buf, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_sign(DIGEST, 33u, buf, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_sign(DIGEST, 32u, NULL, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_sign(DIGEST, 32u, buf, 63u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_cert(NULL));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, cert_page(NULL, 255u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, cert_page(buf, 0u, 0u));
    /* Past the certificate's end, and past what an offset can name. */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, cert_page(buf, 255u, 4000u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, cert_page(buf, 255u, 70000u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, s_page.total);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, s_page.got);
}

static void test_before_init_is_not_provisioned_untouched(void)
{
    uint8_t buf[255];

    tfw_dice_service_clear();
    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_dice_deviceid_pub(buf, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_dice_alias_pub(buf, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_dice_alias_sign(DIGEST, 32u, buf, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, cert_page(buf, sizeof buf, 0u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, s_page.total);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, s_page.got);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_service_init(NULL));
}

/* ---- no answer carries the CDI, the seed or the scalar ------------------- */

static void test_no_answer_carries_a_secret(void)
{
    static uint8_t out[1024];
    uint8_t        seed[32], d[32];
    uint32_t       n;

    TEST_ASSERT_EQUAL(1u, tfw_boot_handoff_device_seed(seed));
    TEST_ASSERT_TRUE(tfw_deviceid_p256_scalar(seed, d));

    memset(out, 0, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_deviceid_pub(out, 64u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_pub(out + 64u, 64u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_sign(DIGEST, 32u, out + 128u, 64u));
    n = 192u + service_cert(out + 192u, sizeof out - 192u, 255u);
    TEST_ASSERT_FALSE(contains(out, n, CDI));
    TEST_ASSERT_FALSE(contains(out, n, seed));
    TEST_ASSERT_FALSE(contains(out, n, d));
    TEST_ASSERT_FALSE(contains(out, n, UDS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deviceid_pub_is_the_non_secure_deviceid);
    RUN_TEST(test_alias_pub_is_the_alias_certificate_subject_key);
    RUN_TEST(test_alias_cert_is_the_non_secure_certificate);
    RUN_TEST(test_alias_sign_is_the_non_secure_signature);
    RUN_TEST(test_alias_sign_verifies_under_the_alias_pub);
    RUN_TEST(test_null_and_short_buffers_are_refused_untouched);
    RUN_TEST(test_before_init_is_not_provisioned_untouched);
    RUN_TEST(test_no_answer_carries_a_secret);
    return UNITY_END();
}
