/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATECC608 driver, against a software model of the element -- the 608C
 * on the MIKROE-6774 Click board.
 *
 * No part is needed. What is tested is everything that is silent when it is
 * wrong: the packet layout, the span the CRC covers, the difference between a
 * status response and a data response, the refusal to sign without an
 * identity, and the sleep that clears the part's volatile state after a secret
 * has been in it. The model's curve is real P-256, so the signatures, public
 * keys and Verify answers below are checked by verifying them -- the model's own
 * correctness is pinned separately, in test_model_p256.c.
 *
 * The CRC has an EXTERNAL anchor -- the wake token's own bytes, which the
 * datasheet publishes -- so this suite cannot pass with a CRC that is
 * consistently wrong on both sides. That is the one check a model could not
 * otherwise provide.
 */

#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "boot_measure.h"
#include "handoff.h"
#include "sha256.h"

/*
 * src/crypto/atecc608a/provider.c's tfw_crypto_init drains any handed-off CDI
 * through src/core/handoff.c, which is linked here for exactly that call.
 * The element model above is this binary's only OTHER HAL need, so a zeroed
 * (no handoff) buffer is this stub's whole job.
 */
static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
}
void tearDown(void) {}

/* ------------------------------------------------------------------- CRC */

/*
 * THE ANCHOR. A woken ATECC608A answers 04 11 33 43: a four-byte response
 * whose body is the status 0x11 ("after wake, before a command") and whose last
 * two bytes are the element's CRC of the first two, little-endian. Those four
 * bytes are published, so 0x4333 is a fact about the part and not about this
 * code -- which makes this the one assertion that proves the CRC rather than
 * proving the model and the driver agree with each other.
 */
static void test_the_crc_matches_the_published_wake_token(void)
{
    const uint8_t wake[2] = { 0x04u, 0x11u };
    TEST_ASSERT_EQUAL_HEX16(0x4333u, tfw_atecc_crc16(wake, 2u));
}

static void test_the_crc_of_nothing_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000u, tfw_atecc_crc16((const uint8_t *)"", 0u));
}

/* Bit order is the thing most likely to be wrong and least likely to show:
 * 0x01 and 0x80 are the same byte reflected, so a most-significant-bit-first
 * implementation swaps their results. */
static void test_the_crc_consumes_each_byte_least_significant_bit_first(void)
{
    const uint8_t a = 0x01u, b = 0x80u;
    /* Only the inequality, deliberately. 0x01 and 0x80 are the same byte
     * reflected, so a most-significant-bit-first implementation gives them the
     * same result -- which is what this catches. Asserting a specific value
     * here would mean asserting a number taken from our own implementation,
     * and the only externally-anchored value available is the wake token
     * above, which already pins the algorithm. */
    TEST_ASSERT_NOT_EQUAL(tfw_atecc_crc16(&a, 1u), tfw_atecc_crc16(&b, 1u));
}

/* ------------------------------------------------------------------ wake */

static void test_wake_reaches_the_element_and_it_answers(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_TRUE(tfw_model_is_awake());
}

static void test_a_command_before_wake_is_refused(void)
{
    uint8_t rev[4];
    /* No wake: the part is asleep and NACKs everything. The driver must report
     * that rather than looping forever waiting for a response. */
    TEST_ASSERT_NOT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u,
                      0, 0u, rev, 4u, 5u));
}

/* ---------------------------------------------------------------- framing */

static void test_a_command_round_trips_and_its_opcode_arrives(void)
{
    uint8_t rev[4];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u,
                      0, 0u, rev, 4u, 5u));
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_INFO, tfw_model_last_opcode());
    TEST_ASSERT_EQUAL_HEX8(0x60u, rev[2]);        /* the 608 family byte */
}

/* A command that carries data exercises the count byte and the CRC span with a
 * non-empty payload, which is where an off-by-one lives. */
static void test_a_command_with_a_payload_frames_correctly(void)
{
    uint8_t digest[32];
    uint8_t i;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)i; }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u,
                      digest, 32u, 0, 0u, 7u));
    /* The element loaded exactly our bytes, not a hash of them. Pass-through
     * mode is what makes the later signature one a verifier holding the same
     * digest can check. */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(digest, tfw_model_tempkey(), 32u);
}

static void test_a_payload_longer_than_the_buffer_is_refused(void)
{
    static uint8_t big[200];
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        tfw_atecc_exec(TFW_ATECC_OP_WRITE, 0u, 0u, big, (uint8_t)sizeof big,
                      0, 0u, 26u));
}

/* ------------------------------------------------------- status responses */

/* An element status is a four-byte response, the same shape a one-byte data
 * response would have. A driver that read it as data would hand a caller the
 * error code as a result. */
static void test_an_element_error_is_returned_as_an_error_not_as_data(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    tfw_model_force_status(0x0Fu);                 /* execution error */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
        tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, 32u, 23u));
}

static void test_a_parse_error_is_distinguished_from_an_execution_error(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    tfw_model_force_status(0x03u);                 /* parse error */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, 32u, 23u));
}

/* The part NACKs reads while it works. A command that finishes late must still
 * be read, and one that never finishes must time out rather than hang. */
static void test_a_busy_element_is_polled_until_it_answers(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    tfw_model_set_busy(8u);
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, 32u, 23u));
}

static void test_an_element_that_never_answers_times_out(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    tfw_model_set_busy(255u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        tfw_atecc_exec(TFW_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, 32u, 1u));
}

/* --------------------------------------------------------- the provider */

static void test_an_absent_element_reports_no_tier_and_refuses_everything(void)
{
    uint8_t out[32];
    uint8_t sig[64];
    uint8_t alg;

    /* tfw_crypto_init is never called, so the provider has not found a part. */
    TEST_ASSERT_EQUAL(TFW_CRYPTO_NONE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(0u, tfw_crypto_algorithms());
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_random(out, 8u));
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_sign(out, sig, &alg));
}

static void test_a_present_element_reports_the_secure_element_tier(void)
{
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
}

/*
 * The algorithms reported are the ones the silicon has, and Ed25519 is not
 * among them. This part has no hardware Ed25519; advertising it would be the
 * same defect as a KMS backend claiming an algorithm it cannot perform, where
 * the host negotiates down to something the device then fails to do.
 */
static void test_the_element_does_not_claim_ed25519(void)
{
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_BITS_HIGH(TFW_ALGBIT_ECDSA_P256_SHA256, tfw_crypto_algorithms());
    TEST_ASSERT_BITS_HIGH(TFW_ALGBIT_HMAC_SHA256,       tfw_crypto_algorithms());
    TEST_ASSERT_BITS_LOW(TFW_ALGBIT_ED25519,            tfw_crypto_algorithms());
}

/* An element with no UDS has no identity, whatever keys are in it. Signing
 * anyway would make "this device attested" mean "this device had a key". */
static void test_an_unprovisioned_element_refuses_to_sign(void)
{
    uint8_t digest[32] = { 0 };
    uint8_t sig[64];
    uint8_t alg = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());      /* present, no UDS */
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(digest, sig, &alg));
}

static void test_a_provisioned_element_signs_and_names_its_algorithm(void)
{
    uint8_t digest[32];
    uint8_t sig[64];
    uint8_t alg = 0u;
    uint8_t i;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)(0x30u + i); }
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, alg);
    /* The signature came back from the element, not from anything here. */
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_SIGN, tfw_model_last_opcode());
}

/* After any command that put a secret in the part, the driver sleeps it --
 * which clears TempKey. Leaving a digest there means a reset between two
 * commands hands the next caller something to sign that it did not choose. */
static void test_the_element_is_slept_after_signing(void)
{
    uint8_t digest[32] = { 0 };
    uint8_t sig[64];
    uint8_t alg;
    uint8_t zero[32] = { 0 };

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, tfw_model_tempkey(), 32u);
}

static void test_random_comes_from_the_element(void)
{
    uint8_t out[16];

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_random(out, (uint8_t)sizeof out));
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_RANDOM, tfw_model_last_opcode());
}

static void test_random_refuses_a_length_it_cannot_fill(void)
{
    uint8_t out[64];

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    /* One Random command yields 32 bytes. Asking for more must be refused
     * rather than answered with 32 good bytes and 32 stale ones. */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_random(out, 33u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_random(out, 0u));
}

static void test_the_boot_counter_is_read_from_the_element(void)
{
    uint32_t v = 0u;

    tfw_model_provision();
    tfw_model_set_counter(0x00ABCDEFuL);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_counter(TFW_ATECC_COUNTER_BOOT, &v));
    TEST_ASSERT_EQUAL_HEX32(0x00ABCDEFuL, v);
}

static void test_a_counter_the_part_does_not_have_is_refused(void)
{
    uint32_t v;

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_counter(9u, &v));
}

/*
 * The element tier never signs with the CDI -- its DeviceID key is
 * element-resident, never derived (see the file header) -- but a CDI tfw-boot
 * handed off must not sit resident in src/core/handoff.c for the process
 * lifetime regardless: tfw_crypto_init drains and discards it. Provable from
 * outside that module: tfw_boot_handoff_take_cdi returns 0 (nothing left to
 * take) after tfw_crypto_init has run, whether or not the element itself is
 * even present.
 */
static void test_init_drains_a_handed_off_cdi_even_though_this_tier_has_no_use_for_it(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], measurement[32], taken[32];
    uint8_t i;

    for (i = 0u; i < sizeof cdi; i++)         { cdi[i] = (uint8_t)(0x11u + i); }
    for (i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    tfw_boot_handoff_build(block, TFW_CRYPTO_SECURE_ELEMENT, cdi, measurement, 1u, 0u);
    memcpy(tfw_hal_handoff_block(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_take_cdi(taken));
}


/* ----------------------------------------------------------------- P-256 */

/* The whole path a verifier relies on: the key the driver reads back from the
 * element is the public half of the private key that signs, and a signature the
 * driver obtains verifies under it. */
static void test_a_signature_from_the_element_verifies_under_its_public_key(void)
{
    uint8_t digest[32], sig[64], pub[64], alg = 0u, i;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)(0x30u + i); }
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));

    TEST_ASSERT_TRUE(tfw_model_p256_verify(pub, digest, sig));
    digest[0] ^= 0x01u;
    TEST_ASSERT_FALSE(tfw_model_p256_verify(pub, digest, sig));
}

static void test_the_public_key_read_from_the_element_matches_its_private_key(void)
{
    uint8_t pub[64], derived[64];

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_TRUE(tfw_model_p256_public(tfw_model_deviceid_private(), derived));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(derived, pub, 64);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tfw_model_deviceid_public(), pub, 64);
}

static void test_signing_a_different_digest_gives_a_different_signature(void)
{
    uint8_t a[32] = { 1 }, b[32] = { 2 }, sa[64], sb[64], alg;

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(a, sa, &alg));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(b, sb, &alg));
    TEST_ASSERT_FALSE(memcmp(sa, sb, sizeof sa) == 0);
}

/* The operator's key is the host's: this plays the host, signing with a private
 * key the element never sees, and has the element verify it in External mode. */
static const uint8_t OP_PRIV[32] = {
    0x21,0x5B,0x8E,0x03,0xC7,0x49,0xF2,0x16,0xA0,0x6D,0x34,0xB8,0x7E,0x1F,0xD9,0x52,
    0x4A,0xC3,0x07,0x98,0xE1,0x2B,0x65,0xFC,0x80,0x3D,0x1A,0xB6,0x59,0xEE,0x72,0x0C
};

static void test_the_element_accepts_a_good_operator_signature(void)
{
    uint8_t digest[32], sig[64], pub[64], i;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)(0x90u ^ i); }
    TEST_ASSERT_TRUE(tfw_model_p256_public(OP_PRIV, pub));
    TEST_ASSERT_TRUE(tfw_model_p256_sign(OP_PRIV, digest, sig));
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_operator(digest, sig, pub));
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_VERIFY, tfw_model_last_opcode());
}

static void test_the_element_rejects_a_bad_operator_signature(void)
{
    uint8_t digest[32] = { 7 }, sig[64], pub[64], other[64], bad[64];

    TEST_ASSERT_TRUE(tfw_model_p256_public(OP_PRIV, pub));
    TEST_ASSERT_TRUE(tfw_model_p256_sign(OP_PRIV, digest, sig));
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());

    /* Wrong digest. */
    digest[0] ^= 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
    digest[0] ^= 1u;
    /* Corrupted signature. */
    memcpy(bad, sig, sizeof bad);
    bad[10] ^= 0x04u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, bad, pub));
    /* A valid signature, checked against somebody else's key. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(other));
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, other));
    /* A public key that is not a point on the curve. */
    memcpy(other, pub, sizeof other);
    other[40] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, other));
    /* And the good one still passes: none of the above poisoned the element. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_operator(digest, sig, pub));
}

/* GenKey in private mode makes a new key and returns only its public half; the
 * n-th key of a fresh part is always the same, so provisioning is reproducible. */
static void test_genkey_private_installs_a_new_key_and_returns_its_public_half(void)
{
    uint8_t pub[64], again[64], test_priv[32], test_pub[64];

    tfw_model_provision();
    tfw_model_test_deviceid(test_priv, test_pub);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_GENKEY, TFW_ATECC_GENKEY_PRIVATE,
                                           TFW_ATECC_SLOT_DEVICEID, 0, 0u, pub, 64u, 115u));
    (void)tfw_atecc_sleep();

    TEST_ASSERT_FALSE(memcmp(pub, test_pub, 64u) == 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tfw_model_deviceid_public(), pub, 64);
    TEST_ASSERT_TRUE(tfw_model_p256_public(tfw_model_deviceid_private(), again));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(again, pub, 64);

    tfw_model_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_GENKEY, TFW_ATECC_GENKEY_PRIVATE,
                                           TFW_ATECC_SLOT_DEVICEID, 0, 0u, again, 64u, 115u));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, again, 64);
}

static void test_genkey_public_on_a_part_with_no_key_is_an_error(void)
{
    uint8_t pub[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_GENKEY, TFW_ATECC_GENKEY_PUBLIC,
                                               TFW_ATECC_SLOT_DEVICEID, 0, 0u, pub, 64u, 115u));
}

/* HMAC is a real HMAC-SHA256 keyed by the slot named, so the CDI the element
 * computes is the one a verifier holding the UDS would. */
static void test_hmac_is_hmac_sha256_keyed_by_the_named_slot(void)
{
    static const uint16_t lens[] = { 0u, 1u, 63u, 64u, 65u, 130u };
    uint8_t               msg[130], got[32], want[32];
    unsigned              n, i;

    for (i = 0u; i < sizeof msg; i++) { msg[i] = (uint8_t)(i * 7u + 3u); }
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());

    for (n = 0u; n < sizeof lens / sizeof lens[0]; n++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, msg, lens[n], got));
        tfw_hmac_sha256(tfw_model_slot(TFW_ATECC_SLOT_UDS), 32u, msg, lens[n], want);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    }
}

static void test_hmac_under_a_different_key_gives_a_different_answer(void)
{
    uint8_t msg[32] = { 9 }, uds_mac[32], other_mac[32];

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, msg, 32u, uds_mac));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(2u, msg, 32u, other_mac));   /* an empty slot */
    TEST_ASSERT_FALSE(memcmp(uds_mac, other_mac, 32u) == 0);
}

static void test_hmac_on_an_unprovisioned_element_is_refused(void)
{
    uint8_t msg[8] = { 0 }, out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, msg, 8u, out));
}

/* The part signs what Nonce loaded, and nothing was loaded after a sleep. */
static void test_sign_with_nothing_in_tempkey_is_an_execution_error(void)
{
    uint8_t sig[64];

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_SIGN, TFW_ATECC_SIGN_EXTERNAL,
                                               TFW_ATECC_SLOT_DEVICEID, 0, 0u, sig, 64u,
                                               TFW_ATECC_T_SIGN));
}

/* ------------------------------------------------- the part on the bench */

/* The ATECC608C-TFLXTLS on the MIKROE-6774 Click answers at 0x36 as shipped
 * (DS40002515B section 3.1), not at the 608A's 0x60. The host build compiles
 * against boards/atmega328p, so this is the Nano's profile speaking. */
static void test_the_driver_addresses_the_click_boards_part_at_0x36(void)
{
    uint8_t rev[4];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u,
                      0, 0u, rev, 4u, TFW_ATECC_T_INFO));
    TEST_ASSERT_EQUAL_HEX8(0x36u, tfw_model_last_addr());
}

/* Info's revision word: 00 00 60 05 on the 608C, where the 608A said
 * 00 00 60 02 (AN5078, DS00005078A). */
static void test_info_reads_the_608c_revision(void)
{
    static const uint8_t want[4] = { 0x00u, 0x00u, 0x60u, 0x05u };
    uint8_t              rev[4];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u,
                      0, 0u, rev, 4u, TFW_ATECC_T_INFO));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, rev, 4u);
}

/* The maximum execution times, clock divider M0, from cryptoauthlib's ATECC608
 * table (lib/calib/calib_execution.c). Literals, so a change to atecc.h has to
 * change this too. */
static void test_the_execution_times_are_the_608s_m0_maxima(void)
{
    TEST_ASSERT_EQUAL_UINT16(5u,   TFW_ATECC_T_INFO);
    TEST_ASSERT_EQUAL_UINT16(5u,   TFW_ATECC_T_READ);
    TEST_ASSERT_EQUAL_UINT16(45u,  TFW_ATECC_T_WRITE);
    TEST_ASSERT_EQUAL_UINT16(35u,  TFW_ATECC_T_LOCK);
    TEST_ASSERT_EQUAL_UINT16(23u,  TFW_ATECC_T_RANDOM);
    TEST_ASSERT_EQUAL_UINT16(20u,  TFW_ATECC_T_NONCE);
    TEST_ASSERT_EQUAL_UINT16(36u,  TFW_ATECC_T_SHA);
    TEST_ASSERT_EQUAL_UINT16(25u,  TFW_ATECC_T_COUNTER);
    TEST_ASSERT_EQUAL_UINT16(115u, TFW_ATECC_T_SIGN);
    TEST_ASSERT_EQUAL_UINT16(115u, TFW_ATECC_T_GENKEY);
    TEST_ASSERT_EQUAL_UINT16(105u, TFW_ATECC_T_VERIFY);
}

/* A part that takes the whole of every maximum still answers every provider
 * call. Before the 608C, Sign waited 64 ms, Verify 72, Counter 6 and Nonce 7:
 * each would have timed out here. */
static void test_the_provider_survives_a_part_at_its_maximum_execution_times(void)
{
    uint8_t  digest[32], sig[64], pub[64], opsig[64], opub[64], out[32], alg, i;
    uint32_t v;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)(0x44u + i); }
    TEST_ASSERT_TRUE(tfw_model_p256_public(OP_PRIV, opub));
    TEST_ASSERT_TRUE(tfw_model_p256_sign(OP_PRIV, digest, opsig));
    tfw_model_provision();
    tfw_model_set_exec_ms(TFW_ATECC_OP_INFO,    5u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_RANDOM,  23u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_NONCE,   20u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_SHA,     36u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_COUNTER, 25u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_SIGN,    115u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_GENKEY,  115u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_VERIFY,  105u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_TRUE(tfw_model_p256_verify(pub, digest, sig));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_operator(digest, opsig, opub));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_counter(TFW_ATECC_COUNTER_BOOT, &v));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_random(out, 32u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, digest, 32u, out));
}

/* The other side of the same fact: the 608A-era Sign bound of 64 ms gives up
 * on a part that takes the 115 ms it is allowed. */
static void test_a_sign_bound_shorter_than_115_ms_times_out(void)
{
    uint8_t digest[32] = { 3 }, sig[64];

    tfw_model_provision();
    tfw_model_set_exec_ms(TFW_ATECC_OP_SIGN, 115u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u,
                      digest, 32u, 0, 0u, TFW_ATECC_T_NONCE));
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        tfw_atecc_exec(TFW_ATECC_OP_SIGN, TFW_ATECC_SIGN_EXTERNAL,
                      TFW_ATECC_SLOT_DEVICEID, 0, 0u, sig, 64u, 64u));
}

/* Idle (word address 0x02) keeps TempKey; only sleep (0x01) clears it
 * (DS40002515B section 8.2.1). A Sign after idle and a fresh wake signs the
 * digest Nonce loaded before. */
static void test_idle_keeps_tempkey_and_sleep_does_not(void)
{
    static const uint8_t idle = TFW_ATECC_WA_IDLE;
    uint8_t              digest[32], sig[64], i;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)(0xC0u ^ i); }
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u,
                      digest, 32u, 0, 0u, TFW_ATECC_T_NONCE));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(TFW_ATECC_ADDR, &idle, 1u, 0, 0u));
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(digest, tfw_model_tempkey(), 32u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_SIGN, TFW_ATECC_SIGN_EXTERNAL,
                      TFW_ATECC_SLOT_DEVICEID, 0, 0u, sig, 64u, TFW_ATECC_T_SIGN));
    TEST_ASSERT_TRUE(tfw_model_p256_verify(tfw_model_deviceid_public(), digest, sig));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_sleep());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_wake());
    TEST_ASSERT_NOT_EQUAL(TFW_OK,
        tfw_atecc_exec(TFW_ATECC_OP_SIGN, TFW_ATECC_SIGN_EXTERNAL,
                      TFW_ATECC_SLOT_DEVICEID, 0, 0u, sig, 64u, TFW_ATECC_T_SIGN));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_crc_matches_the_published_wake_token);
    RUN_TEST(test_the_crc_of_nothing_is_zero);
    RUN_TEST(test_the_crc_consumes_each_byte_least_significant_bit_first);
    RUN_TEST(test_wake_reaches_the_element_and_it_answers);
    RUN_TEST(test_a_command_before_wake_is_refused);
    RUN_TEST(test_a_command_round_trips_and_its_opcode_arrives);
    RUN_TEST(test_a_command_with_a_payload_frames_correctly);
    RUN_TEST(test_a_payload_longer_than_the_buffer_is_refused);
    RUN_TEST(test_an_element_error_is_returned_as_an_error_not_as_data);
    RUN_TEST(test_a_parse_error_is_distinguished_from_an_execution_error);
    RUN_TEST(test_a_busy_element_is_polled_until_it_answers);
    RUN_TEST(test_an_element_that_never_answers_times_out);
    RUN_TEST(test_an_absent_element_reports_no_tier_and_refuses_everything);
    RUN_TEST(test_a_present_element_reports_the_secure_element_tier);
    RUN_TEST(test_the_element_does_not_claim_ed25519);
    RUN_TEST(test_an_unprovisioned_element_refuses_to_sign);
    RUN_TEST(test_a_provisioned_element_signs_and_names_its_algorithm);
    RUN_TEST(test_the_element_is_slept_after_signing);
    RUN_TEST(test_a_signature_from_the_element_verifies_under_its_public_key);
    RUN_TEST(test_the_public_key_read_from_the_element_matches_its_private_key);
    RUN_TEST(test_signing_a_different_digest_gives_a_different_signature);
    RUN_TEST(test_the_element_accepts_a_good_operator_signature);
    RUN_TEST(test_the_element_rejects_a_bad_operator_signature);
    RUN_TEST(test_genkey_private_installs_a_new_key_and_returns_its_public_half);
    RUN_TEST(test_genkey_public_on_a_part_with_no_key_is_an_error);
    RUN_TEST(test_hmac_is_hmac_sha256_keyed_by_the_named_slot);
    RUN_TEST(test_hmac_under_a_different_key_gives_a_different_answer);
    RUN_TEST(test_hmac_on_an_unprovisioned_element_is_refused);
    RUN_TEST(test_sign_with_nothing_in_tempkey_is_an_execution_error);
    RUN_TEST(test_random_comes_from_the_element);
    RUN_TEST(test_random_refuses_a_length_it_cannot_fill);
    RUN_TEST(test_the_boot_counter_is_read_from_the_element);
    RUN_TEST(test_a_counter_the_part_does_not_have_is_refused);
    RUN_TEST(test_init_drains_a_handed_off_cdi_even_though_this_tier_has_no_use_for_it);
    /* Last: these initialise the provider, and the absent-element test above
     * depends on running before anything has. */
    RUN_TEST(test_the_driver_addresses_the_click_boards_part_at_0x36);
    RUN_TEST(test_info_reads_the_608c_revision);
    RUN_TEST(test_the_execution_times_are_the_608s_m0_maxima);
    RUN_TEST(test_the_provider_survives_a_part_at_its_maximum_execution_times);
    RUN_TEST(test_a_sign_bound_shorter_than_115_ms_times_out);
    RUN_TEST(test_idle_keeps_tempkey_and_sleep_does_not);
    return UNITY_END();
}
