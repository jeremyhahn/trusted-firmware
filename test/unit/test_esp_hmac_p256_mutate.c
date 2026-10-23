/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 HMAC/P-256 crypto tier,
 * src/hal/esp32/crypto_hmac_p256.c.
 *
 * The unit had no host build. It is built here on the real SHA-256, the real
 * P-256 DeviceID derivation and micro-ecc, over stand-ins for ESP-IDF's eFuse
 * key search, HMAC peripheral, RNG and log (test/mock/esp32/hmac/), with the
 * layer-0 handoff and tfw_secure_zero implemented below. It holds:
 *
 *   - a CDI only from a valid handoff for this tier, taken only then; the
 *     tier's slot 0 HMAC under it, any other slot out of range, none without
 *     it; its fingerprint logged as the first 16 bytes of
 *     HMAC(CDI, "tfw-cdi-fingerprint-v1") in hex, and zeroed;
 *   - the UDS as the HMAC_UP key block, its key id the block less KEY0; the
 *     DeviceID seed as HMAC(UDS, "tfw-deviceid-v1"), zeroed after each use;
 *     the public point derived from it, its fingerprint logged; a part with no
 *     such block, or a peripheral that refuses it, left with no DeviceID;
 *   - ES256 only with both a UDS and a CDI, the signature the derivation's own,
 *     a refused seed a crypto error; micro-ecc blinded by the part's RNG;
 *   - the algorithm bits as exactly what is held, re-read at every init;
 *   - RNG and SHA-256 passed through; counters and verification unsupported.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_random.h"
#include "uECC.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "crypto/sha256.h"
#include "crypto/deviceid_p256.h"
#include "../../src/core/handoff.h"
#include "../../src/core/secure_zero.h"

/* ------------------------------------------------------------------ handoff */

static const uint8_t *tier_ptr;
static uint8_t        tier_val;
static uint8_t        take_ok;
static unsigned       take_calls;
static uint8_t        cdi_in[32];

const uint8_t *tfw_boot_handoff_tier(void) { return tier_ptr; }

uint8_t tfw_boot_handoff_take_cdi(uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    take_calls++;
    if (!take_ok) {
        return 0u;
    }
    memcpy(out, cdi_in, sizeof cdi_in);
    return 1u;
}

/* ------------------------------------------------------------- secure zero */

#define ZMAX 16u

static struct {
    const void *p;
    size_t      n;
    uint8_t     was[64];
} zeroed[ZMAX];
static unsigned nzeroed;

void tfw_secure_zero(void *p, size_t n)
{
    if (nzeroed < ZMAX) {
        zeroed[nzeroed].p = p;
        zeroed[nzeroed].n = n;
        memcpy(zeroed[nzeroed].was, p, n < 64u ? n : 64u);
        nzeroed++;
    }
    memset(p, 0, n);
}

static const void *hmac_out;   /* where the last seed was written */

/* Whether a buffer of n bytes holding exactly `was` was zeroed, at `at` if
 * that is not NULL. */
static int was_zeroed_at(const void *at, const uint8_t *was, size_t n)
{
    unsigned i;

    for (i = 0u; i < nzeroed; i++) {
        if (zeroed[i].n == n && memcmp(zeroed[i].was, was, n) == 0 &&
            (at == NULL || zeroed[i].p == at)) {
            return 1;
        }
    }
    return 0;
}

#define was_zeroed(was, n) was_zeroed_at(NULL, (was), (n))
/* The seed buffer the unit itself handed the HMAC peripheral. */
#define seed_zeroed(was)   was_zeroed_at(hmac_out, (was), 32u)

/* ---------------------------------------------------------------- the part */

static int               find_ok;
static esp_efuse_block_t found_block;
static unsigned          find_calls;
static esp_err_t         hmac_err;
static unsigned          hmac_calls;
static unsigned          rand_calls;
static size_t            rand_len;

bool esp_efuse_find_purpose(esp_efuse_purpose_t purpose, esp_efuse_block_t *block)
{
    find_calls++;
    TEST_ASSERT_EQUAL(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, purpose);
    if (!find_ok) {
        return false;
    }
    *block = found_block;
    return true;
}

/* The seed a key id gives; a refusal leaves bytes no key gives. */
static void seed_of(unsigned key, uint8_t seed[32])
{
    unsigned i;

    for (i = 0u; i < 32u; i++) {
        seed[i] = (uint8_t)(0x11u * (key + 1u) + 7u * i);
    }
}

esp_err_t esp_hmac_calculate(hmac_key_id_t key_id, const void *message, size_t message_len,
                             uint8_t *hmac)
{
    hmac_calls++;
    hmac_out = hmac;
    TEST_ASSERT_EQUAL_UINT(15u, (unsigned)message_len);
    TEST_ASSERT_EQUAL_MEMORY("tfw-deviceid-v1", message, 15u);
    if (hmac_err != ESP_OK) {
        memset(hmac, 0xE7, 32u);
        return hmac_err;
    }
    TEST_ASSERT_LESS_THAN_INT(HMAC_KEY_MAX, key_id);
    seed_of((unsigned)key_id, hmac);
    return ESP_OK;
}

void esp_fill_random(void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t   i;

    rand_calls++;
    rand_len = len;
    for (i = 0u; i < len; i++) {
        p[i] = (uint8_t)(0x3Bu + 13u * (rand_calls + i));
    }
}

#define LMAX 8u

static struct {
    char level;
    char msg[160];
} logs[LMAX];
static unsigned nlogs;

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
{
    va_list ap;

    TEST_ASSERT_EQUAL_STRING("tfw-id", tag);
    TEST_ASSERT_LESS_THAN_UINT(LMAX, nlogs);
    logs[nlogs].level = level;
    va_start(ap, fmt);
    (void)vsnprintf(logs[nlogs].msg, sizeof logs[nlogs].msg, fmt, ap);
    va_end(ap);
    nlogs++;
}

static void assert_log(unsigned i, char level, const char *msg)
{
    TEST_ASSERT_LESS_THAN_UINT(nlogs, i);
    TEST_ASSERT_EQUAL_CHAR(level, logs[i].level);
    TEST_ASSERT_EQUAL_STRING(msg, logs[i].msg);
}

static void hex16(char out[33], const uint8_t in[32])
{
    unsigned i;

    for (i = 0u; i < 16u; i++) {
        (void)snprintf(&out[2u * i], 3u, "%02x", in[i]);
    }
}

/* ------------------------------------------------------------------- tests */

static uint8_t seed_k3[32], pub_k3[64];

static void reset_part(void)
{
    nzeroed = nlogs = 0u;
    take_calls = find_calls = hmac_calls = rand_calls = 0u;
    rand_len = 0u;
    tier_val = (uint8_t)TFW_CRYPTO_SECURE_ELEMENT;
    tier_ptr = &tier_val;
    take_ok  = 1u;
    find_ok  = 1;
    found_block = EFUSE_BLK_KEY3;
    hmac_err = ESP_OK;
}

void setUp(void)
{
    unsigned i;

    for (i = 0u; i < 32u; i++) {
        cdi_in[i] = (uint8_t)(0xC0u ^ (5u * i));
    }
    seed_of(3u, seed_k3);
    tfw_deviceid_p256_public(seed_k3, pub_k3);
    reset_part();
}

void tearDown(void) {}

static void init_both(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_HEX16(TFW_ALGBIT_ECDSA_P256_SHA256 | TFW_ALGBIT_HMAC_SHA256,
                            tfw_crypto_algorithms());
}

static void test_a_handoff_cdi_and_an_hmac_up_key_give_both(void)
{
    uint8_t fp[32], pub[64];
    char    want[160], h[33];

    init_both();
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    TEST_ASSERT_EQUAL_UINT(1u, take_calls);
    TEST_ASSERT_EQUAL_UINT(1u, find_calls);
    TEST_ASSERT_EQUAL_UINT(1u, hmac_calls);

    /* the seed and the CDI fingerprint, zeroed */
    TEST_ASSERT_TRUE(seed_zeroed(seed_k3));
    tfw_hmac_sha256(cdi_in, 32u, (const uint8_t *)"tfw-cdi-fingerprint-v1", 22u, fp);
    TEST_ASSERT_TRUE(was_zeroed(fp, 32u));

    TEST_ASSERT_EQUAL_UINT(2u, nlogs);
    hex16(h, fp);
    (void)snprintf(want, sizeof want, "cdi hmac-fingerprint=%s", h);
    assert_log(0u, 'I', want);
    tfw_sha256(pub_k3, 64u, fp);
    hex16(h, fp);
    (void)snprintf(want, sizeof want, "deviceid p256 sha256(x||y)=%s from eFuse BLOCK7", h);
    assert_log(1u, 'I', want);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pub_k3, pub, 64u);
}

static void test_the_key_id_is_the_block_less_key0(void)
{
    static const esp_efuse_block_t blocks[] = { EFUSE_BLK_KEY0, EFUSE_BLK_KEY5 };
    static const unsigned          keys[]   = { 0u, 5u };
    static const char *const       names[]  = { "BLOCK4", "BLOCK9" };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        uint8_t seed[32], want_pub[64], pub[64];

        seed_of(keys[i], seed);
        tfw_deviceid_p256_public(seed, want_pub);
        reset_part();
        found_block = blocks[i];
        init_both();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want_pub, pub, 64u);
        TEST_ASSERT_NOT_NULL(strstr(logs[1].msg, names[i]));
    }
}

static void test_slot_0_is_the_hmac_under_the_cdi(void)
{
    uint8_t data[300], want[32], out[32];
    unsigned i;

    for (i = 0u; i < sizeof data; i++) {
        data[i] = (uint8_t)i;
    }
    init_both();
    tfw_hmac_sha256(cdi_in, 32u, data, 300u, want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, data, 300u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32u);
    tfw_hmac_sha256(cdi_in, 32u, data, 1u, want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, data, 1u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32u);

    memset(out, 0x77, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(1u, data, 1u, out));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(255u, data, 1u, out));
    TEST_ASSERT_EACH_EQUAL_HEX8(0x77u, out, 32u);
}

static void test_a_signature_is_es256_by_the_deviceid(void)
{
    static const uint8_t digest[32] = { 0x9Fu, 0x01u, 0x22u, 0x5Eu, 0x80u, 0xFFu, 0x00u, 0x31u };
    uint8_t want[64], sig[64], alg = 0xEEu;

    tfw_deviceid_p256_sign(seed_k3, digest, want);
    init_both();
    nzeroed = 0u;
    hmac_calls = rand_calls = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, alg);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, sig, 64u);
    TEST_ASSERT_EQUAL_INT(1, uECC_verify(pub_k3, digest, 32u, sig, uECC_secp256r1()));
    TEST_ASSERT_EQUAL_UINT(1u, hmac_calls);
    TEST_ASSERT_TRUE(seed_zeroed(seed_k3));
    /* micro-ecc's blinding came from the part's RNG */
    TEST_ASSERT_GREATER_THAN_UINT(0u, rand_calls);
}

static void test_a_refused_seed_at_signing_is_a_crypto_error(void)
{
    static const uint8_t digest[32] = { 1u };
    uint8_t garbage[32], sig[64], alg = 0xEEu;

    init_both();
    nzeroed  = 0u;
    hmac_err = ESP_FAIL;
    memset(sig, 0x44, sizeof sig);
    memset(garbage, 0xE7, sizeof garbage);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, alg);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x44u, sig, 64u);
    TEST_ASSERT_TRUE(seed_zeroed(garbage));
}

static void assert_no_cdi(void)
{
    uint8_t out[32], sig[64], pub[64], alg = 0xEEu;

    TEST_ASSERT_EQUAL_HEX16(TFW_ALGBIT_ECDSA_P256_SHA256, tfw_crypto_algorithms());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, cdi_in, 4u, out));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(1u, cdi_in, 4u, out));
    hmac_calls = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(cdi_in, sig, &alg));
    TEST_ASSERT_EQUAL_UINT(0u, hmac_calls);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, alg);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pub_k3, pub, 64u);
}

static void test_no_handoff_leaves_no_cdi(void)
{
    init_both();
    reset_part();
    tier_ptr = NULL;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT(0u, take_calls);
    assert_log(0u, 'W', "cdi: no valid handoff from layer 0");
    assert_no_cdi();
}

static void test_a_handoff_for_another_tier_leaves_no_cdi(void)
{
    static const uint8_t tiers[] = { 0u, 1u, 3u, 0x82u };
    unsigned i;

    for (i = 0u; i < sizeof tiers; i++) {
        char want[64];

        reset_part();
        init_both();
        reset_part();
        tier_val = tiers[i];
        TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
        TEST_ASSERT_EQUAL_UINT(0u, take_calls);
        (void)snprintf(want, sizeof want, "cdi: none in the handoff (tier %u)", tiers[i]);
        assert_log(0u, 'W', want);
        assert_no_cdi();
    }
}

static void test_a_handoff_with_no_cdi_leaves_none(void)
{
    init_both();
    reset_part();
    take_ok = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT(1u, take_calls);
    assert_log(0u, 'W', "cdi: none in the handoff (tier 2)");
    assert_no_cdi();
}

static void assert_no_uds(void)
{
    uint8_t sig[64], pub[64], alg = 0xEEu;

    TEST_ASSERT_EQUAL_HEX16(TFW_ALGBIT_HMAC_SHA256, tfw_crypto_algorithms());
    memset(pub, 0x66, sizeof pub);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EACH_EQUAL_HEX8(0x66u, pub, 64u);
    hmac_calls = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(cdi_in, sig, &alg));
    TEST_ASSERT_EQUAL_UINT(0u, hmac_calls);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, alg);
}

static void test_a_part_with_no_hmac_up_key_has_no_deviceid(void)
{
    init_both();
    reset_part();
    find_ok = 0;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT(0u, hmac_calls);
    TEST_ASSERT_EQUAL_UINT(2u, nlogs);
    assert_log(1u, 'W', "deviceid: no HMAC_UP key in eFuse: this part has no UDS");
    assert_no_uds();
}

static void test_a_refused_uds_key_has_no_deviceid(void)
{
    uint8_t garbage[32];

    memset(garbage, 0xE7, sizeof garbage);
    init_both();
    reset_part();
    found_block = EFUSE_BLK_KEY0;
    hmac_err    = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT(1u, hmac_calls);
    TEST_ASSERT_TRUE(seed_zeroed(garbage));
    TEST_ASSERT_EQUAL_UINT(2u, nlogs);
    assert_log(1u, 'E', "deviceid: HMAC peripheral refused key block 4");
    assert_no_uds();
}

static void test_neither_held_gives_no_algorithm(void)
{
    init_both();
    reset_part();
    tier_ptr = NULL;
    find_ok  = 0;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_HEX16(0u, tfw_crypto_algorithms());
}

static void test_random_and_sha256_pass_through(void)
{
    uint8_t out[255], want[32], digest[32];
    unsigned i;

    memset(out, 0, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_random(out, 255u));
    TEST_ASSERT_EQUAL_UINT(1u, rand_calls);
    TEST_ASSERT_EQUAL_UINT(255u, (unsigned)rand_len);
    for (i = 0u; i < 255u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x3Bu + 13u * (1u + i)), out[i]);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_random(out, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)rand_len);

    tfw_sha256(out, 255u, want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(out, 255u, digest));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, digest, 32u);
}

static void test_counters_and_verification_are_unsupported(void)
{
    static const uint8_t z[64] = { 0u };
    uint32_t c = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_counter(0u, &c));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, c);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_operator(z, z, z));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_image(z, z));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_handoff_cdi_and_an_hmac_up_key_give_both);
    RUN_TEST(test_the_key_id_is_the_block_less_key0);
    RUN_TEST(test_slot_0_is_the_hmac_under_the_cdi);
    RUN_TEST(test_a_signature_is_es256_by_the_deviceid);
    RUN_TEST(test_a_refused_seed_at_signing_is_a_crypto_error);
    RUN_TEST(test_no_handoff_leaves_no_cdi);
    RUN_TEST(test_a_handoff_for_another_tier_leaves_no_cdi);
    RUN_TEST(test_a_handoff_with_no_cdi_leaves_none);
    RUN_TEST(test_a_part_with_no_hmac_up_key_has_no_deviceid);
    RUN_TEST(test_a_refused_uds_key_has_no_deviceid);
    RUN_TEST(test_neither_held_gives_no_algorithm);
    RUN_TEST(test_random_and_sha256_pass_through);
    RUN_TEST(test_counters_and_verification_are_unsupported);
    return UNITY_END();
}
