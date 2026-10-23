/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The software crypto provider, src/crypto/soft/provider.c, against seeded
 * mutants (#163). test_crypto_soft pins each path once; this suite pins what a
 * mutant of each path would change: a refusal that writes before it refuses, a
 * check dropped or reordered, a key or digest length off by one, a handoff
 * branch that falls through to the local UDS, a state not reset by init.
 *
 * Mutants tried, each killed by the test named:
 *   init: drop `provisioned = 0u` (line 69)    -> a_mismatched_handoff_after_a_uds_init_unprovisions,
 *                                                 the_handoff_cdi_is_taken_once
 *   init: tier test `==` -> `!=`               -> the_software_handoff_signs_with_its_cdi
 *   init: `else if (handoff_tier)` narrowed to the element tier
 *                                              -> a_handoff_naming_no_tier_refuses_even_with_a_local_uds
 *   init: mismatch branch falls through        -> a_mismatched_handoff_after_a_uds_init_unprovisions
 *   init: mismatched CDI not taken             -> a_mismatched_handoff_cdi_is_drained
 *   init: UDS NULL check dropped               -> every test that inits with no UDS (faults)
 *   init: either provisioned = 1u -> 0u         -> the_fallback_macs..., the_software_handoff_signs...
 *   init: sentinel provisions anyway           -> a_sentinel_handoff_after_a_uds_init_unprovisions
 *   init: UDS key length 32 -> 31              -> every_uds_byte_reaches_the_cdi
 *   init: label length +-1, hash skipped       -> the_fallback_macs_with_the_derived_cdi_and_signs_nothing
 *   hmac: slot test dropped / `!= 0` -> `> 1`   -> a_refused_hmac_leaves_its_output
 *   hmac: output written before the checks     -> a_refused_hmac_leaves_its_output
 *   hmac: CDI key length 32 -> 31               -> the_software_handoff_signs_with_its_cdi
 *   hmac: data length ignored                  -> an_empty_message_has_its_own_mac
 *   sign: HMAC over 31 digest bytes, padding or MAC copy 32 -> 31: id_hmac.c's,
 *         not linked here since the soft root signs ES256 with the P-256 DeviceID
 *         (id_p256.c); its digest bytes -> the_last_digest_byte_moves_the_signature,
 *         its key, the handed-off DeviceID seed (#173) -> the_software_handoff_signs_with_its_cdi,
 *         the_refusals_read_and_write_nothing; no seed, no signature
 *                                              -> the_fallback_macs_with_the_derived_cdi_and_signs_nothing
 *   sign: alg_id written before the refusal    -> a_refused_sign_leaves_sig_and_alg
 *   sign: pad written before the refusal       -> a_refused_sign_leaves_sig_and_alg
 *   sha256: length ignored / off by one        -> the_hash_of_nothing, the_hash_of_abc
 *   random / pubkey / counter / verify_image write or read, then refuse
 *                                              -> the_refusals_read_and_write_nothing
 *   algorithms reports none                    -> the_refusals_read_and_write_nothing
 * Equivalent, so not killable from outside: tfw_secure_zero of the drained
 * mismatched CDI (line 126) clears a stack buffer never read again; dropping
 * sign's own `!provisioned` check (line 202) leaves the identical refusal to
 * tfw_crypto_hmac, which answers before writing; dropping its `st != TFW_OK`
 * check (line 206) is dead once that check passed, as slot 0 is never refused.
 *
 * The expected digests and MACs are literals computed outside this code
 * (Python's hmac/hashlib), never by the provider they pin.
 */
#include <string.h>
#include "unity.h"
#include "hal_mock.h"
#include "uds_mock.h"
#include "handoff.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "boot_measure.h"
#include "deviceid_p256.h"
#include "sha256.h"

static uint8_t TEST_UDS[32];   /* uds[i] = i + 1 */
static uint8_t TEST_CDI[32];   /* cdi[i] = 0xD0 + i */
static uint8_t DIGEST[32];     /* 0x42 x 32 */

/* HMAC-SHA256(TEST_CDI, 0x42 x 32) */
static const uint8_t HANDOFF_SIGN_42[32] = {
    0x6F, 0xAB, 0x1F, 0x36, 0x0E, 0xD3, 0x71, 0x42, 0x9F, 0xAD, 0x6D, 0x27, 0x50, 0x7B, 0xA8, 0x9C,
    0x6E, 0xE5, 0xEA, 0x0E, 0xC9, 0x30, 0x9E, 0x11, 0xB4, 0x00, 0x39, 0x6D, 0x61, 0x5F, 0xAC, 0x90
};

/* HMAC-SHA256(HMAC-SHA256(TEST_UDS, SHA-256("tfw-unmeasured-application-v1")), 0x42 x 32) */
static const uint8_t FALLBACK_SIGN_42[32] = {
    0xA9, 0x2B, 0x21, 0x6D, 0x40, 0xCF, 0xF3, 0x2D, 0xF4, 0x94, 0x96, 0x17, 0x40, 0x7E, 0x1C, 0x94,
    0x59, 0xD5, 0x32, 0x18, 0xBD, 0xFC, 0xB5, 0x21, 0xDF, 0x83, 0xF9, 0xC0, 0x8F, 0x97, 0x08, 0x45
};

/* HMAC-SHA256(the same fallback CDI, "") */
static const uint8_t FALLBACK_HMAC_EMPTY[32] = {
    0xFF, 0x83, 0x67, 0x54, 0x49, 0xBA, 0x7A, 0xBF, 0x79, 0x92, 0xAE, 0x23, 0xB4, 0x1E, 0xCC, 0x9C,
    0x05, 0xAB, 0xFA, 0x9B, 0x7D, 0xA0, 0xC1, 0x3B, 0x4D, 0x7F, 0x53, 0xAB, 0x98, 0x6E, 0xBA, 0x7C
};

/* SHA-256(""), FIPS 180-4 example. */
static const uint8_t SHA_EMPTY[32] = {
    0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
    0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
};

/* SHA-256("abc"), FIPS 180-4 example. */
static const uint8_t SHA_ABC[32] = {
    0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
    0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
};

/* x || y of HMAC-SHA256(TEST_UDS, "tfw-deviceid-v1") * G on P-256: the soft
 * root's DeviceID, computed with Go's crypto/elliptic, not with this code. */
static const uint8_t DEVICEID_PUB[64] = {
    0x85, 0x4B, 0x68, 0xC5, 0x31, 0xBB, 0xA2, 0x46, 0xDB, 0x18, 0x1F, 0x4D, 0xC9, 0x10, 0xBF, 0x82,
    0x7E, 0x0A, 0x0A, 0x76, 0xE9, 0x86, 0xCF, 0x5A, 0x29, 0x20, 0xE0, 0xAF, 0x14, 0x09, 0x7B, 0x0C,
    0x11, 0x22, 0xA2, 0x46, 0xF7, 0x66, 0x7A, 0xFD, 0xE6, 0xA5, 0x9F, 0x5F, 0x64, 0x0E, 0xC1, 0xEB,
    0xE8, 0x50, 0x6A, 0xB0, 0x62, 0x2E, 0xB1, 0x0B, 0xD2, 0x74, 0x45, 0x91, 0x4D, 0x9D, 0x8C, 0xDF
};

/* r || s of the RFC 6979 ES256 signature of 0x42 x 32 under that DeviceID,
 * from Go's crypto/ecdsa (Sign with a nil rand). The soft root signs with the
 * DeviceID, so the CDI reaches only the HMAC. */
static const uint8_t DEVICEID_SIG_42[64] = {
    0x2B, 0x80, 0x30, 0x21, 0xF7, 0xD3, 0xE2, 0xEE, 0x5C, 0xD0, 0x35, 0x44, 0x2B, 0x63, 0xCE, 0xA5,
    0x39, 0x28, 0x5D, 0xCD, 0x31, 0x0A, 0x90, 0xA9, 0x91, 0xB6, 0x4D, 0x5F, 0x56, 0x1D, 0x91, 0x94,
    0xA2, 0x2D, 0x45, 0x5D, 0x5F, 0x4D, 0x98, 0xFF, 0xD7, 0xB3, 0xA5, 0x4C, 0xF9, 0x7E, 0x96, 0xE8,
    0x1A, 0x50, 0x47, 0x4B, 0x09, 0xC6, 0x1D, 0x96, 0x85, 0xAE, 0x8E, 0x57, 0x79, 0x5C, 0xA3, 0x19
};

void setUp(void)
{
    for (uint8_t i = 0u; i < sizeof TEST_UDS; i++) { TEST_UDS[i] = (uint8_t)(i + 1u); }
    for (uint8_t i = 0u; i < sizeof TEST_CDI; i++) { TEST_CDI[i] = (uint8_t)(0xD0u + i); }
    memset(DIGEST, 0x42, sizeof DIGEST);
    tfw_mock_reset();
    tfw_boot_handoff_init();        /* the zeroed handoff area: no valid handoff */
    tfw_mock_set_uds(0);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
}

void tearDown(void) {}

static void hand_off(uint8_t tier, const uint8_t cdi[32])
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t measurement[32];
    uint8_t seed[TFW_BOOT_SEED_LEN];

    for (uint8_t i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    tfw_boot_handoff_build(block, tier, cdi, measurement, 1u, 0u);
    /* The DeviceID seed tfw-boot derives from the UDS and hands on (#173): the
     * application never holds the UDS itself, so the P-256 DeviceID that
     * DEVICEID_PUB and DEVICEID_SIG_42 pin reaches it only this way. */
    tfw_hmac_sha256(TEST_UDS, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                    TFW_DEVICEID_LABEL_LEN, seed);
    tfw_boot_handoff_set_seed(block, seed);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();
}

/* A software-tier handoff carrying TEST_CDI and TEST_UDS's DeviceID seed: the
 * path that signs, now the UDS fallback has no DeviceID (#173). */
static void provision_from_handoff(void)
{
    tfw_mock_set_uds(TEST_UDS);
    hand_off(TFW_CRYPTO_SOFTWARE, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
}

/* Provisioned from the local UDS, so a later refusal is the init's doing. */
static void provision_from_uds(void)
{
    uint8_t out[32];

    tfw_mock_set_uds(TEST_UDS);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, DIGEST, sizeof DIGEST, out));
}

static void assert_refused_and_untouched(void)
{
    uint8_t sig[64], out[32];
    uint8_t alg = 0xEEu;

    memset(sig, 0xA5, sizeof sig);
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, DIGEST, sizeof DIGEST, out));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, sig, sizeof sig);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, alg);
}

/* expect is the CDI's MAC of DIGEST; the signature is the DeviceID's. */
static void assert_signs(const uint8_t expect[32])
{
    uint8_t sig[64], out[32];
    uint8_t alg = TFW_ALG_NONE;

    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, DIGEST, sizeof DIGEST, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 32);
    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DEVICEID_SIG_42, sig, 64);
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, alg);
}

/* ---- init ---------------------------------------------------------------- */

static void test_the_fallback_macs_with_the_derived_cdi_and_signs_nothing(void)
{
    uint8_t sig[64], out[32];
    uint8_t alg = 0xEEu;

    provision_from_uds();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, DIGEST, sizeof DIGEST, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FALLBACK_SIGN_42, out, 32);
    /* No handoff, no DeviceID seed: the UDS never reaches the application's
     * DeviceID (#173), so there is nothing to sign with. */
    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, sig, sizeof sig);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, alg);
}

static void test_every_uds_byte_reaches_the_cdi(void)
{
    uint8_t out[32];

    for (uint8_t i = 0u; i < sizeof TEST_UDS; i++) {
        setUp();
        TEST_UDS[i] ^= 0x01u;
        tfw_mock_set_uds(TEST_UDS);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
        TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, DIGEST, sizeof DIGEST, out));
        TEST_ASSERT_FALSE_MESSAGE(memcmp(out, FALLBACK_SIGN_42, 32) == 0, "a UDS byte did not reach the CDI");
        TEST_UDS[i] ^= 0x01u;
    }
}

static void test_the_software_handoff_signs_with_its_cdi(void)
{
    tfw_mock_set_uds(TEST_UDS);
    hand_off(TFW_CRYPTO_SOFTWARE, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    assert_signs(HANDOFF_SIGN_42);
}

static void test_a_mismatched_handoff_after_a_uds_init_unprovisions(void)
{
    provision_from_uds();
    hand_off(TFW_CRYPTO_SECURE_ELEMENT, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    assert_refused_and_untouched();
}

static void test_a_sentinel_handoff_after_a_uds_init_unprovisions(void)
{
    uint8_t zero[32];

    memset(zero, 0, sizeof zero);
    provision_from_uds();
    hand_off(TFW_CRYPTO_SOFTWARE, zero);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    assert_refused_and_untouched();
}

/* The mismatched CDI is drained, not left for a later taker. */
static void test_a_mismatched_handoff_cdi_is_drained(void)
{
    uint8_t taken[32];

    memset(taken, 0xA5, sizeof taken);
    hand_off(TFW_CRYPTO_SECURE_ELEMENT, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_take_cdi(taken));
}

/* A valid handoff naming any tier but this one is a mismatch, not only one
 * naming the element. */
static void test_a_handoff_naming_no_tier_refuses_even_with_a_local_uds(void)
{
    tfw_mock_set_uds(TEST_UDS);
    hand_off(TFW_CRYPTO_NONE, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    assert_refused_and_untouched();
}

/* tfw_boot_handoff_take_cdi drains: a second init on the same handoff finds
 * the tier and no CDI, and refuses rather than keep the first init's key or
 * fall back to the local UDS. */
static void test_the_handoff_cdi_is_taken_once(void)
{
    tfw_mock_set_uds(TEST_UDS);
    hand_off(TFW_CRYPTO_SOFTWARE, TEST_CDI);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    assert_signs(HANDOFF_SIGN_42);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    assert_refused_and_untouched();
}

/* ---- hmac ---------------------------------------------------------------- */

static void test_a_refused_hmac_leaves_its_output(void)
{
    static const uint8_t SLOTS[] = { 1u, 2u, 0x80u, 0xFFu };
    uint8_t out[32];

    provision_from_uds();
    for (uint8_t i = 0u; i < sizeof SLOTS; i++) {
        memset(out, 0xA5, sizeof out);
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(SLOTS[i], DIGEST, sizeof DIGEST, out));
        TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof out);
    }
    tfw_mock_set_uds(0);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(1u, DIGEST, sizeof DIGEST, out));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof out);
}

static void test_an_empty_message_has_its_own_mac(void)
{
    uint8_t out[32];

    provision_from_uds();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, DIGEST, 0u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FALLBACK_HMAC_EMPTY, out, 32);
}

/* ---- sign ---------------------------------------------------------------- */

static void test_the_last_digest_byte_moves_the_signature(void)
{
    uint8_t sig[64];
    uint8_t alg;

    provision_from_handoff();
    DIGEST[31] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_FALSE(memcmp(sig, DEVICEID_SIG_42, 64) == 0);
    DIGEST[31] ^= 0x01u;
    DIGEST[0]  ^= 0x80u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_FALSE(memcmp(sig, DEVICEID_SIG_42, 64) == 0);
}

static void test_a_refused_sign_leaves_sig_and_alg(void)
{
    assert_refused_and_untouched();
}

/* ---- the rest ------------------------------------------------------------ */

static void test_the_hash_of_nothing(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(DIGEST, 0u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_EMPTY, out, 32);
}

static void test_the_hash_of_abc(void)
{
    static const uint8_t ABC[3] = { 'a', 'b', 'c' };
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(ABC, sizeof ABC, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_ABC, out, 32);
}

/* NULL everywhere: a refusal that wrote, or read, before refusing faults. */
static void test_the_refusals_read_and_write_nothing(void)
{
    uint8_t pub[64];

    provision_from_handoff();
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_random(0, 0xFFu));
    /* pubkey is no refusal now the soft root has a P-256 DeviceID: it writes
     * all 64 bytes, so it gets a real buffer. */
    memset(pub, 0xA5, sizeof pub);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DEVICEID_PUB, pub, sizeof pub);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_counter(0xFFu, 0));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_operator(DIGEST, 0, 0));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_image(DIGEST, 0));
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL_HEX16(TFW_ALGBIT_ECDSA_P256_SHA256, tfw_crypto_algorithms());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_fallback_macs_with_the_derived_cdi_and_signs_nothing);
    RUN_TEST(test_every_uds_byte_reaches_the_cdi);
    RUN_TEST(test_the_software_handoff_signs_with_its_cdi);
    RUN_TEST(test_a_mismatched_handoff_after_a_uds_init_unprovisions);
    RUN_TEST(test_a_sentinel_handoff_after_a_uds_init_unprovisions);
    RUN_TEST(test_a_mismatched_handoff_cdi_is_drained);
    RUN_TEST(test_a_handoff_naming_no_tier_refuses_even_with_a_local_uds);
    RUN_TEST(test_the_handoff_cdi_is_taken_once);
    RUN_TEST(test_a_refused_hmac_leaves_its_output);
    RUN_TEST(test_an_empty_message_has_its_own_mac);
    RUN_TEST(test_the_last_digest_byte_moves_the_signature);
    RUN_TEST(test_a_refused_sign_leaves_sig_and_alg);
    RUN_TEST(test_the_hash_of_nothing);
    RUN_TEST(test_the_hash_of_abc);
    RUN_TEST(test_the_refusals_read_and_write_nothing);
    return UNITY_END();
}
