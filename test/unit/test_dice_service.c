/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure-world DICE service (#590 slice b) on host.
 *
 * The DeviceID reference is today's non-secure path, linked beside the
 * service: the soft root's P-256 identity (src/crypto/soft/id_p256.c) over this
 * suite's own seed handoff, and SEED_PUB is the point test_soft_id_p256 pins
 * for the fixed UDS 00..1F. The alias reference is the same P-256 derivation
 * over HMAC-SHA256(CDI, TFW_DICE_ALIAS_LABEL), and alias.c's TBS digest and
 * certificate signed with it: the alias key is never the DeviceID key.
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

/* The alias seed the service must derive from CDI. */
static void alias_seed_ref(uint8_t out[32])
{
    tfw_hmac_sha256(CDI, 32u, (const uint8_t *)TFW_DICE_ALIAS_LABEL,
                    TFW_DICE_ALIAS_LABEL_LEN, out);
}

/* The whole alias certificate for NONCE, signed with the alias key. */
static uint16_t reference_cert(uint8_t *buf, uint16_t cap, uint8_t digest[32],
                               uint8_t sig[64])
{
    uint8_t pub[64], seed[32], dev[32], got = 0u;
    uint16_t total = 0u, off = 0u;
    tfw_alias_parts_t p = { NONCE, UEID, FW_HASH, pub, sig, TFW_CRYPTO_SOFTWARE };

    /* The anchored chain: the alias key is the subject, the DeviceID key
     * signs the TBS. */
    alias_seed_ref(seed);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_handoff_device_seed(dev));
    TEST_ASSERT_TRUE(tfw_deviceid_p256_public(seed, pub));
    memset(sig, 0, 64u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_alias_tbs_digest(&p, digest));
    TEST_ASSERT_TRUE(tfw_deviceid_p256_sign(dev, digest, sig));
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

static void test_alias_pub_is_the_cdi_derived_key(void)
{
    uint8_t got[64], ref[64], seed[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_pub(got, sizeof got));
    alias_seed_ref(seed);
    TEST_ASSERT_TRUE(tfw_deviceid_p256_public(seed, ref));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, 64);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(SEED_PUB, got, 64));
}

static void test_alias_cert_is_the_reference_chain(void)
{
    static uint8_t ref[1024], got[1024];
    uint8_t        digest[32], sig[64];
    uint16_t       n = reference_cert(ref, sizeof ref, digest, sig);

    TEST_ASSERT_EQUAL_UINT32(n, service_cert(got, sizeof got, 255u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, n);
    memset(got, 0, sizeof got);
    TEST_ASSERT_EQUAL_UINT32(n, service_cert(got, sizeof got, 17u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, n);
}

static void test_alias_cert_verifies_under_the_deviceid_pub_only(void)
{
    static uint8_t ref[1024], got[1024];
    uint8_t        digest[32], sig[64], dev[64], alias[64];
    uint16_t       n = reference_cert(ref, sizeof ref, digest, sig);

    TEST_ASSERT_EQUAL_UINT32(n, service_cert(got, sizeof got, 255u));
    TEST_ASSERT_TRUE(contains(got, n, sig));
    TEST_ASSERT_TRUE(contains(got, n, sig + 32));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_deviceid_pub(dev, sizeof dev));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_pub(alias, sizeof alias));
    TEST_ASSERT_TRUE(uECC_verify(dev, digest, 32u, sig, uECC_secp256r1()));
    TEST_ASSERT_FALSE(uECC_verify(alias, digest, 32u, sig, uECC_secp256r1()));
}

static void test_alias_cert_subject_is_the_alias_pub(void)
{
    static uint8_t got[1024];
    uint8_t        dev[64], alias[64];
    uint32_t       n = service_cert(got, sizeof got, 255u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_deviceid_pub(dev, sizeof dev));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_pub(alias, sizeof alias));
    TEST_ASSERT_TRUE(contains(got, n, alias));
    TEST_ASSERT_TRUE(contains(got, n, alias + 32));
    TEST_ASSERT_FALSE(contains(got, n, dev));
    TEST_ASSERT_FALSE(contains(got, n, dev + 32));
}

static void test_alias_sign_is_the_alias_key_signature(void)
{
    uint8_t got[64], ref[64], seed[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_sign(DIGEST, 32u, got, sizeof got));
    alias_seed_ref(seed);
    TEST_ASSERT_TRUE(tfw_deviceid_p256_sign(seed, DIGEST, ref));
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

/* Codex round 1 on #592: the sign call is no DeviceID signing oracle. */
static void test_alias_sign_does_not_verify_under_the_deviceid_pub(void)
{
    uint8_t dev[64], sig[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_deviceid_pub(dev, sizeof dev));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_sign(DIGEST, 32u, sig, sizeof sig));
    TEST_ASSERT_FALSE(uECC_verify(dev, DIGEST, 32u, sig, uECC_secp256r1()));
}

static void test_alias_sign_before_alias_derivation_is_refused_untouched(void)
{
    uint8_t           buf[64];
    tfw_dice_inputs_t no_cdi = { UDS, NULL, UEID, FW_HASH, TFW_CRYPTO_SOFTWARE };

    tfw_dice_service_clear();
    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_dice_alias_sign(DIGEST, 32u, buf, 64u));
    /* An init without a CDI derives no alias key, so still nothing is signed. */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_service_init(&no_cdi));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_dice_alias_sign(DIGEST, 32u, buf, 64u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
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
    uint8_t        seed[32], d[32], aseed[32], ad[32];
    uint32_t       n;

    TEST_ASSERT_EQUAL(1u, tfw_boot_handoff_device_seed(seed));
    TEST_ASSERT_TRUE(tfw_deviceid_p256_scalar(seed, d));
    alias_seed_ref(aseed);
    TEST_ASSERT_TRUE(tfw_deviceid_p256_scalar(aseed, ad));

    memset(out, 0, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_deviceid_pub(out, 64u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_pub(out + 64u, 64u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_alias_sign(DIGEST, 32u, out + 128u, 64u));
    n = 192u + service_cert(out + 192u, sizeof out - 192u, 255u);
    TEST_ASSERT_FALSE(contains(out, n, CDI));
    TEST_ASSERT_FALSE(contains(out, n, seed));
    TEST_ASSERT_FALSE(contains(out, n, d));
    TEST_ASSERT_FALSE(contains(out, n, aseed));
    TEST_ASSERT_FALSE(contains(out, n, ad));
    TEST_ASSERT_FALSE(contains(out, n, UDS));
}

/* dice_service.c:47-48: a refused init clears the service first, so the last good one is gone. */
static void test_an_init_without_a_uds_is_refused_and_forgets_the_last_one(void)
{
    uint8_t           buf[64];
    tfw_dice_inputs_t in = { NULL, CDI, UEID, FW_HASH, TFW_CRYPTO_SOFTWARE };

    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_service_init(&in));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_dice_deviceid_pub(buf, 64u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
}

/* dice_service.c:48: the in->ueid == NULL clause. */
static void test_an_init_without_a_ueid_is_refused(void)
{
    uint8_t           buf[64];
    tfw_dice_inputs_t in = { UDS, CDI, NULL, FW_HASH, TFW_CRYPTO_SOFTWARE };

    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_service_init(&in));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_dice_alias_pub(buf, 64u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
}

/* dice_service.c:49: the in->fw_hash == NULL clause. */
static void test_an_init_without_a_fw_hash_is_refused(void)
{
    uint8_t           buf[255];
    tfw_dice_inputs_t in = { UDS, CDI, UEID, NULL, TFW_CRYPTO_SOFTWARE };

    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_service_init(&in));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, cert_page(buf, 255u, 0u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, s_page.total);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, s_page.got);
}

/* dice_service.c:121: TFW_NS_CHECK(sig, cap) with cap 0. */
static void test_alias_sign_into_a_zero_cap_is_refused(void)
{
    uint8_t buf[64];

    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_sign(DIGEST, 32u, buf, 0u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
}

/* dice_service.c:90, 120-121 and 156: the wrap clause of the host TFW_NS_CHECK; never dereferenced. */
static void test_a_buffer_that_wraps_the_address_space_is_refused(void)
{
    uint8_t        buf[64];
    uint8_t       *hi_out  = (uint8_t *)(UINTPTR_MAX - 31u);
    const uint8_t *hi_in   = (const uint8_t *)(UINTPTR_MAX - 15u);
    uint8_t       *hi_page = (uint8_t *)(UINTPTR_MAX - 99u);

    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_deviceid_pub(hi_out, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_pub(hi_out, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_sign(hi_in, 32u, buf, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_alias_sign(DIGEST, 32u, hi_out, 64u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, cert_page(hi_page, 255u, 0u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, s_page.total);
    TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEu, s_page.got);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_deviceid_pub_is_the_non_secure_deviceid);
    RUN_TEST(test_alias_pub_is_the_cdi_derived_key);
    RUN_TEST(test_alias_cert_is_the_reference_chain);
    RUN_TEST(test_alias_cert_verifies_under_the_deviceid_pub_only);
    RUN_TEST(test_alias_cert_subject_is_the_alias_pub);
    RUN_TEST(test_alias_sign_is_the_alias_key_signature);
    RUN_TEST(test_alias_sign_verifies_under_the_alias_pub);
    RUN_TEST(test_alias_sign_does_not_verify_under_the_deviceid_pub);
    RUN_TEST(test_alias_sign_before_alias_derivation_is_refused_untouched);
    RUN_TEST(test_null_and_short_buffers_are_refused_untouched);
    RUN_TEST(test_before_init_is_not_provisioned_untouched);
    RUN_TEST(test_no_answer_carries_a_secret);
    RUN_TEST(test_an_init_without_a_uds_is_refused_and_forgets_the_last_one);
    RUN_TEST(test_an_init_without_a_ueid_is_refused);
    RUN_TEST(test_an_init_without_a_fw_hash_is_refused);
    RUN_TEST(test_alias_sign_into_a_zero_cap_is_refused);
    RUN_TEST(test_a_buffer_that_wraps_the_address_space_is_refused);
    return UNITY_END();
}
