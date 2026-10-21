/*
 * The soft root's identity as the P-256 DeviceID, src/crypto/soft/id_p256.c,
 * driven directly (#48, #130): the public point and the ES256 signature for a
 * known UDS, the algorithm id on the wire, and the seed and scalar zeroed.
 *
 * Linked without src/core/secure_zero.c and without a provider: the suite's
 * own tfw_secure_zero records what it is handed, and its own
 * tfw_boot_handoff_device_seed stands for the handoff tfw-boot leaves: the
 * seed HMAC-SHA256(UDS, TFW_DEVICEID_LABEL), or none when uds_ptr is NULL, as
 * boot/tier/cdi_soft.c derives it. The UDS (uds[i] = i), point, digest and
 * signature are test_deviceid_p256.c's pinned literals, computed outside this
 * code; they are repeated here, not derived.
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

#define ZERO_MAX  8
#define ZERO_COPY 64

static unsigned zero_calls;
static size_t   zero_len[ZERO_MAX];
static uint8_t  zero_before[ZERO_MAX][ZERO_COPY];
static int      zero_after_ok[ZERO_MAX];

void tfw_secure_zero(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    size_t            i;
    int               ok = 1;

    if (zero_calls < ZERO_MAX) {
        zero_len[zero_calls] = n;
        memcpy(zero_before[zero_calls], p, n < ZERO_COPY ? n : ZERO_COPY);
    }
    for (i = 0; i < n; i++) { v[i] = 0u; }
    for (i = 0; i < n; i++) { ok = ok && v[i] == 0u; }
    if (zero_calls < ZERO_MAX) {
        zero_after_ok[zero_calls] = ok;
    }
    zero_calls++;
}

static uint8_t        UDS[32];
static const uint8_t *uds_ptr;

uint8_t tfw_boot_handoff_device_seed(uint8_t out[32])
{
    if (!uds_ptr) {
        return 0u;
    }
    tfw_hmac_sha256(uds_ptr, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                    TFW_DEVICEID_LABEL_LEN, out);
    return 1u;
}

/* HMAC-SHA256(uds[i] = i, TFW_DEVICEID_LABEL): the seed, in range, so the scalar. */
static const uint8_t SEED[32] = {
    0xEF,0x21,0x71,0x8D,0xDA,0x8A,0x78,0x11,0xA2,0xB8,0x32,0xA1,0xD0,0x11,0xC1,0x5C,
    0xF1,0x22,0xC6,0x35,0x09,0x44,0xEC,0xC9,0x9A,0x6E,0x4B,0xEB,0x48,0x37,0x72,0xA1
};

static const uint8_t SEED_PUB[64] = {
    0xC4,0x57,0x5D,0x64,0xA4,0xD8,0x9A,0x2A,0x53,0x2B,0x03,0x82,0xF3,0xA0,0x02,0xA2,
    0xB7,0x8D,0x4E,0xCB,0xC1,0x70,0x50,0x7E,0xDE,0x97,0x91,0xEB,0xFC,0xC9,0x7F,0x0E,
    0x55,0x4D,0xFC,0xFD,0x69,0x4E,0x65,0x91,0x04,0xB0,0xDE,0xEE,0xAC,0xCE,0x0C,0x47,
    0x00,0x2A,0x51,0x6E,0xA8,0xE5,0xF0,0xCE,0x96,0x22,0x0E,0x84,0x1C,0xC7,0x5C,0xA6
};

/* SHA-256("tfw-deviceid-selftest-v1") */
static const uint8_t DIGEST[32] = {
    0xFF,0x30,0xD6,0xCE,0x12,0x94,0xB5,0x48,0x66,0xAE,0x73,0x43,0x80,0xC4,0xCC,0xC9,
    0x0F,0x40,0x7A,0xF7,0xD1,0xA3,0xB6,0x1F,0x43,0x2E,0xD8,0xBC,0x5C,0x4A,0x16,0x3F
};

static const uint8_t SEED_SIG[64] = {
    0x96,0x62,0x51,0x68,0x83,0xB4,0x00,0x18,0x6B,0xAB,0x0E,0x3D,0x8A,0xB5,0xC6,0x67,
    0xE7,0x12,0xA0,0x8E,0x7C,0xFE,0x7A,0x7F,0xE6,0xA6,0x2B,0x8F,0x82,0x75,0xFC,0x80,
    0xB5,0x34,0xF8,0x87,0x2F,0x97,0x02,0x8A,0xC6,0x74,0xFB,0xF0,0x73,0x60,0xB6,0x31,
    0x44,0x8D,0xAF,0x0A,0x1A,0x01,0xDA,0x16,0x33,0x96,0xCB,0xD7,0xF0,0x01,0xBB,0xFB
};

/* Ignored by id_p256.c: the DeviceID is the UDS's, not the CDI's. */
static uint8_t CDI[32];

void setUp(void)
{
    for (uint8_t i = 0u; i < sizeof UDS; i++) { UDS[i] = i; }
    memset(CDI, 0xD0, sizeof CDI);
    uds_ptr = UDS;
    zero_calls = 0u;
    memset(zero_len, 0, sizeof zero_len);
    memset(zero_before, 0, sizeof zero_before);
    memset(zero_after_ok, 0, sizeof zero_after_ok);
}

void tearDown(void) {}

static void test_the_only_algorithm_is_es256(void)
{
    TEST_ASSERT_EQUAL_HEX16(TFW_ALGBIT_ECDSA_P256_SHA256, tfw_soft_id_algorithms());
}

static void test_the_pubkey_of_a_known_uds_is_the_pinned_point(void)
{
    uint8_t pub[64];

    memset(pub, 0xA5, sizeof pub);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_soft_id_pubkey(pub));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEED_PUB, pub, 64);
    TEST_ASSERT_TRUE(uECC_valid_public_key(pub, uECC_secp256r1()));
}

static void test_the_signature_is_pinned_es256_and_verifies_under_the_pubkey(void)
{
    uint8_t pub[64], sig[64];
    uint8_t alg = TFW_ALG_NONE;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_soft_id_pubkey(pub));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_soft_id_sign(CDI, DIGEST, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEED_SIG, sig, 64);
    TEST_ASSERT_TRUE(uECC_verify(pub, DIGEST, 32u, sig, uECC_secp256r1()));
}

static void test_the_alg_id_is_0x01(void)
{
    uint8_t sig[64];
    uint8_t alg = TFW_ALG_NONE;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_soft_id_sign(CDI, DIGEST, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(0x01, alg);
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, alg);
}

static void test_the_signature_does_not_depend_on_the_cdi(void)
{
    uint8_t sig[64];
    uint8_t alg = TFW_ALG_NONE;

    memset(CDI, 0x5A, sizeof CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_soft_id_sign(CDI, DIGEST, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEED_SIG, sig, 64);
}

/* Every buffer handed to tfw_secure_zero is zero afterwards; the last is the
 * seed, and one before it held the same 32 bytes as the scalar. */
static void check_seed_and_scalar_zeroed(void)
{
    unsigned i, scalars = 0u;

    TEST_ASSERT_TRUE(zero_calls >= 2u);
    TEST_ASSERT_TRUE(zero_calls <= ZERO_MAX);
    for (i = 0u; i < zero_calls; i++) {
        TEST_ASSERT_TRUE(zero_after_ok[i]);
        if (zero_len[i] == 32u && memcmp(zero_before[i], SEED, 32) == 0) {
            scalars++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(32u, zero_len[zero_calls - 1u]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEED, zero_before[zero_calls - 1u], 32);
    TEST_ASSERT_TRUE(scalars >= 2u);
}

static void test_pubkey_zeroes_the_seed_and_the_scalar(void)
{
    uint8_t pub[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_soft_id_pubkey(pub));
    check_seed_and_scalar_zeroed();
}

static void test_sign_zeroes_the_seed_and_the_scalar(void)
{
    uint8_t sig[64];
    uint8_t alg = TFW_ALG_NONE;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_soft_id_sign(CDI, DIGEST, sig, &alg));
    check_seed_and_scalar_zeroed();
}

static void test_no_uds_is_not_provisioned_and_writes_nothing(void)
{
    uint8_t pub[64], sig[64];
    uint8_t alg = TFW_ALG_NONE;

    uds_ptr = 0;
    memset(pub, 0xA5, sizeof pub);
    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_soft_id_pubkey(pub));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_soft_id_sign(CDI, DIGEST, sig, &alg));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, pub, sizeof pub);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, sig, sizeof sig);
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_NONE, alg);
    TEST_ASSERT_EQUAL_UINT(0u, zero_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_only_algorithm_is_es256);
    RUN_TEST(test_the_pubkey_of_a_known_uds_is_the_pinned_point);
    RUN_TEST(test_the_signature_is_pinned_es256_and_verifies_under_the_pubkey);
    RUN_TEST(test_the_alg_id_is_0x01);
    RUN_TEST(test_the_signature_does_not_depend_on_the_cdi);
    RUN_TEST(test_pubkey_zeroes_the_seed_and_the_scalar);
    RUN_TEST(test_sign_zeroes_the_seed_and_the_scalar);
    RUN_TEST(test_no_uds_is_not_provisioned_and_writes_nothing);
    return UNITY_END();
}
