/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 `hmac-p256` provider (#48, row esp32-p4), src/hal/esp32/
 * crypto_hmac_p256.c, on the host: the HMAC peripheral, the eFuse key-purpose
 * lookup and the RNG are this suite's (headers in test/mock/esp32/), and the
 * handoff is the real one (src/core/handoff.c) fed a block boot_handoff.c
 * builds, as layer 0 does on the part.
 *
 * The UDS is 0x00..0x1f, the one test_deviceid_p256 and the QEMU harness
 * (test/integration/esp32/qemu_test.py, DEVICEID_PUB) pin, so the point and the
 * ES256 signature below are the same literals: a change that moves the
 * DeviceID on the P4 fails here before it reaches QEMU.
 */
#include <stdarg.h>
#include <string.h>
#include "unity.h"
#include "uECC.h"
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_random.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "../../src/core/handoff.h"
#include "../../boot/boot_measure.h"
#include "../../src/crypto/sha256.h"

/* test_deviceid_p256.c's SEED_PUB: the P-256 point of HMAC(0x00..0x1f,
 * "tfw-deviceid-v1"), from an independent derivation. */
static const uint8_t UDS_PUB[64] = {
    0xC4,0x57,0x5D,0x64,0xA4,0xD8,0x9A,0x2A,0x53,0x2B,0x03,0x82,0xF3,0xA0,0x02,0xA2,
    0xB7,0x8D,0x4E,0xCB,0xC1,0x70,0x50,0x7E,0xDE,0x97,0x91,0xEB,0xFC,0xC9,0x7F,0x0E,
    0x55,0x4D,0xFC,0xFD,0x69,0x4E,0x65,0x91,0x04,0xB0,0xDE,0xEE,0xAC,0xCE,0x0C,0x47,
    0x00,0x2A,0x51,0x6E,0xA8,0xE5,0xF0,0xCE,0x96,0x22,0x0E,0x84,0x1C,0xC7,0x5C,0xA6
};
/* test_deviceid_p256.c's DIGEST, SHA-256("tfw-deviceid-selftest-v1"), and
 * SEED_SIG, its RFC 6979 ES256 signature under that DeviceID. */
static const uint8_t DIGEST[32] = {
    0xFF,0x30,0xD6,0xCE,0x12,0x94,0xB5,0x48,0x66,0xAE,0x73,0x43,0x80,0xC4,0xCC,0xC9,
    0x0F,0x40,0x7A,0xF7,0xD1,0xA3,0xB6,0x1F,0x43,0x2E,0xD8,0xBC,0x5C,0x4A,0x16,0x3F
};
static const uint8_t UDS_SIG[64] = {
    0x96,0x62,0x51,0x68,0x83,0xB4,0x00,0x18,0x6B,0xAB,0x0E,0x3D,0x8A,0xB5,0xC6,0x67,
    0xE7,0x12,0xA0,0x8E,0x7C,0xFE,0x7A,0x7F,0xE6,0xA6,0x2B,0x8F,0x82,0x75,0xFC,0x80,
    0xB5,0x34,0xF8,0x87,0x2F,0x97,0x02,0x8A,0xC6,0x74,0xFB,0xF0,0x73,0x60,0xB6,0x31,
    0x44,0x8D,0xAF,0x0A,0x1A,0x01,0xDA,0x16,0x33,0x96,0xCB,0xD7,0xF0,0x01,0xBB,0xFB
};

/* ---- the part: one HMAC_UP key block, or none ---------------------------- */

static uint8_t           uds[32];
static int               uds_burned;
static esp_efuse_block_t uds_block;
static int               hmac_refuse_from;   /* the call that fails; 0 = none */
static int               hmac_calls;
static hmac_key_id_t     hmac_last_key;
static uint8_t           test_handoff[TFW_BOOT_HANDOFF_LEN];
static int               log_count[256];

bool esp_efuse_find_purpose(esp_efuse_purpose_t purpose, esp_efuse_block_t *block)
{
    if (purpose != ESP_EFUSE_KEY_PURPOSE_HMAC_UP || !uds_burned) {
        return false;
    }
    *block = uds_block;
    return true;
}

/* The peripheral: a MAC under the key in the named block, which software never
 * reads. A key id that is not that block's has no HMAC_UP key behind it. */
esp_err_t esp_hmac_calculate(hmac_key_id_t key_id, const void *message,
                             size_t message_len, uint8_t *hmac)
{
    hmac_calls++;
    hmac_last_key = key_id;
    if (hmac_refuse_from != 0 && hmac_calls >= hmac_refuse_from) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!uds_burned || (int)key_id != (int)uds_block - (int)EFUSE_BLK_KEY0) {
        return ESP_ERR_INVALID_ARG;
    }
    tfw_hmac_sha256(uds, (uint8_t)sizeof uds, (const uint8_t *)message,
                    (uint16_t)message_len, hmac);
    return ESP_OK;
}

void esp_fill_random(void *buf, size_t len)
{
    memset(buf, 0xA5, len);
}

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
{
    (void)tag; (void)fmt;
    log_count[(unsigned char)level]++;
}

uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

/* ---- helpers --------------------------------------------------------------- */

static void burn_uds(esp_efuse_block_t block)
{
    uint8_t i;

    for (i = 0u; i < 32u; i++) { uds[i] = i; }
    uds_burned = 1;
    uds_block  = block;
}

/* Layer 0's handoff for `tier` with a CDI of `cdi_byte` (0 is tfw-boot's own
 * "unprovisioned" signal), then the provider's init. */
static void boot_with(uint8_t tier, uint8_t cdi_byte)
{
    uint8_t cdi[32], measurement[32];

    memset(cdi, cdi_byte, sizeof cdi);
    memset(measurement, 0x3E, sizeof measurement);
    tfw_boot_handoff_build(test_handoff, tier, cdi, measurement, 1u, 0u);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
}

/* No valid handoff at all: the block layer 0 never wrote. */
static void boot_without_handoff(void)
{
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
}

static int verifies(const uint8_t pub[64], const uint8_t digest[32], const uint8_t sig[64])
{
    return uECC_valid_public_key(pub, uECC_secp256r1()) &&
           uECC_verify(pub, digest, 32u, sig, uECC_secp256r1());
}

void setUp(void)
{
    memset(uds, 0, sizeof uds);
    uds_burned       = 0;
    uds_block        = EFUSE_BLK_KEY0;
    hmac_refuse_from = 0;
    hmac_calls       = 0;
    hmac_last_key    = HMAC_KEY_MAX;
    memset(log_count, 0, sizeof log_count);
}

void tearDown(void) {}

/* ---- a provisioned part, booted by layer 0 ---------------------------------- */

static void test_the_deviceid_is_the_pinned_point_of_the_uds(void)
{
    uint8_t pub[64];

    burn_uds(EFUSE_BLK_KEY1);   /* where the QEMU harness burns it */
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0xC1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UDS_PUB, pub, 64);
    TEST_ASSERT_EQUAL(HMAC_KEY1, hmac_last_key);
    TEST_ASSERT_EQUAL_UINT16(TFW_ALGBIT_ECDSA_P256_SHA256 | TFW_ALGBIT_HMAC_SHA256,
                             tfw_crypto_algorithms());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
}

static void test_the_uds_is_found_in_any_key_block(void)
{
    uint8_t pub[64];

    burn_uds(EFUSE_BLK_KEY5);
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0xC1);
    TEST_ASSERT_EQUAL(HMAC_KEY5, hmac_last_key);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UDS_PUB, pub, 64);
}

static void test_sign_is_the_pinned_es256_signature_and_verifies(void)
{
    uint8_t sig[64], again[64], pub[64], alg = 0u;

    burn_uds(EFUSE_BLK_KEY1);
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0xC1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_EQUAL_UINT8(TFW_ALG_ECDSA_P256_SHA256, alg);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UDS_SIG, sig, 64);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_TRUE(verifies(pub, DIGEST, sig));
    /* RFC 6979: the RNG only blinds, so the same bytes every time. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(DIGEST, again, &alg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sig, again, 64);
}

static void test_the_deviceid_does_not_move_with_the_measurement(void)
{
    uint8_t a[64], b[64];

    burn_uds(EFUSE_BLK_KEY1);
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0xC1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(a));
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0x5D);   /* an updated image */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(b));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, b, 64);
}

/* ---- no UDS, no CDI, a refusing peripheral ---------------------------------- */

static void test_no_handoff_leaves_no_cdi_and_no_signature(void)
{
    uint8_t pub[64], sig[64], out[32], alg = 0u;

    burn_uds(EFUSE_BLK_KEY1);
    boot_without_handoff();
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, DIGEST, 32u, out));
    /* The eFuse holds a key, but the measured chain did not run: no signature. */
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(UDS_PUB, pub, 64);
    TEST_ASSERT_EQUAL_UINT16(TFW_ALGBIT_ECDSA_P256_SHA256, tfw_crypto_algorithms());
}

static void test_an_all_zero_cdi_is_not_a_cdi(void)
{
    uint8_t sig[64], out[32], alg = 0u;

    burn_uds(EFUSE_BLK_KEY1);
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0x00);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, DIGEST, 32u, out));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(DIGEST, sig, &alg));
}

static void test_a_peripheral_that_refuses_at_init_leaves_no_deviceid(void)
{
    uint8_t pub[64];

    burn_uds(EFUSE_BLK_KEY1);
    hmac_refuse_from = 1;
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0xC1);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_UINT16(TFW_ALGBIT_HMAC_SHA256, tfw_crypto_algorithms());
    TEST_ASSERT_GREATER_THAN(0, log_count['E']);
}

static void test_a_peripheral_that_refuses_at_sign_is_a_crypto_error(void)
{
    uint8_t sig[64], alg = 0u;

    burn_uds(EFUSE_BLK_KEY1);
    hmac_refuse_from = 2;   /* init's seed passes, the signing seed does not */
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0xC1);
    alg = 0x7Eu;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_EQUAL_UINT8(0x7Eu, alg);
}

static void test_a_second_init_forgets_the_first_boots_identity(void)
{
    uint8_t pub[64];

    burn_uds(EFUSE_BLK_KEY1);
    boot_with((uint8_t)TFW_CRYPTO_SECURE_ELEMENT, 0xC1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    uds_burned = 0;
    boot_without_handoff();
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_crypto_algorithms());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_deviceid_is_the_pinned_point_of_the_uds);
    RUN_TEST(test_the_uds_is_found_in_any_key_block);
    RUN_TEST(test_sign_is_the_pinned_es256_signature_and_verifies);
    RUN_TEST(test_the_deviceid_does_not_move_with_the_measurement);
    RUN_TEST(test_no_handoff_leaves_no_cdi_and_no_signature);
    RUN_TEST(test_an_all_zero_cdi_is_not_a_cdi);
    RUN_TEST(test_a_peripheral_that_refuses_at_init_leaves_no_deviceid);
    RUN_TEST(test_a_peripheral_that_refuses_at_sign_is_a_crypto_error);
    RUN_TEST(test_a_second_init_forgets_the_first_boots_identity);
    return UNITY_END();
}
