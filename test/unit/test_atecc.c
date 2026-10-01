/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATECC608A driver, against a software model of the element.
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
#include "ag/crypto.h"
#include "ag/protocol.h"
#include "ag/boot.h"
#include "boot_measure.h"
#include "handoff.h"
#include "sha256.h"

/*
 * src/crypto/atecc608a/provider.c's ag_crypto_init drains any handed-off CDI
 * through src/core/handoff.c, which is linked here for exactly that call.
 * The element model above is this binary's only OTHER HAL need, so a zeroed
 * (no handoff) buffer is this stub's whole job.
 */
static uint8_t test_handoff[AG_BOOT_HANDOFF_LEN];
uint8_t *ag_hal_handoff_block(void) { return test_handoff; }

void setUp(void)
{
    ag_model_reset();
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
    TEST_ASSERT_EQUAL_HEX16(0x4333u, ag_atecc_crc16(wake, 2u));
}

static void test_the_crc_of_nothing_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000u, ag_atecc_crc16((const uint8_t *)"", 0u));
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
    TEST_ASSERT_NOT_EQUAL(ag_atecc_crc16(&a, 1u), ag_atecc_crc16(&b, 1u));
}

/* ------------------------------------------------------------------ wake */

static void test_wake_reaches_the_element_and_it_answers(void)
{
    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    TEST_ASSERT_TRUE(ag_model_is_awake());
}

static void test_a_command_before_wake_is_refused(void)
{
    uint8_t rev[4];
    /* No wake: the part is asleep and NACKs everything. The driver must report
     * that rather than looping forever waiting for a response. */
    TEST_ASSERT_NOT_EQUAL(AG_OK,
        ag_atecc_exec(AG_ATECC_OP_INFO, AG_ATECC_INFO_REVISION, 0u,
                      0, 0u, rev, 4u, 5u));
}

/* ---------------------------------------------------------------- framing */

static void test_a_command_round_trips_and_its_opcode_arrives(void)
{
    uint8_t rev[4];

    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    TEST_ASSERT_EQUAL(AG_OK,
        ag_atecc_exec(AG_ATECC_OP_INFO, AG_ATECC_INFO_REVISION, 0u,
                      0, 0u, rev, 4u, 5u));
    TEST_ASSERT_EQUAL_HEX8(AG_ATECC_OP_INFO, ag_model_last_opcode());
    TEST_ASSERT_EQUAL_HEX8(0x60u, rev[2]);        /* the 608 family byte */
}

/* A command that carries data exercises the count byte and the CRC span with a
 * non-empty payload, which is where an off-by-one lives. */
static void test_a_command_with_a_payload_frames_correctly(void)
{
    uint8_t digest[32];
    uint8_t i;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)i; }
    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    TEST_ASSERT_EQUAL(AG_OK,
        ag_atecc_exec(AG_ATECC_OP_NONCE, AG_ATECC_NONCE_PASSTHRU, 0u,
                      digest, 32u, 0, 0u, 7u));
    /* The element loaded exactly our bytes, not a hash of them. Pass-through
     * mode is what makes the later signature one a verifier holding the same
     * digest can check. */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(digest, ag_model_tempkey(), 32u);
}

static void test_a_payload_longer_than_the_buffer_is_refused(void)
{
    static uint8_t big[200];
    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
        ag_atecc_exec(AG_ATECC_OP_WRITE, 0u, 0u, big, (uint8_t)sizeof big,
                      0, 0u, 26u));
}

/* ------------------------------------------------------- status responses */

/* An element status is a four-byte response, the same shape a one-byte data
 * response would have. A driver that read it as data would hand a caller the
 * error code as a result. */
static void test_an_element_error_is_returned_as_an_error_not_as_data(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    ag_model_force_status(0x0Fu);                 /* execution error */
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED,
        ag_atecc_exec(AG_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, 32u, 23u));
}

static void test_a_parse_error_is_distinguished_from_an_execution_error(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    ag_model_force_status(0x03u);                 /* parse error */
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
        ag_atecc_exec(AG_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, 32u, 23u));
}

/* The part NACKs reads while it works. A command that finishes late must still
 * be read, and one that never finishes must time out rather than hang. */
static void test_a_busy_element_is_polled_until_it_answers(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    ag_model_set_busy(8u);
    TEST_ASSERT_EQUAL(AG_OK,
        ag_atecc_exec(AG_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, 32u, 23u));
}

static void test_an_element_that_never_answers_times_out(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    ag_model_set_busy(255u);
    TEST_ASSERT_EQUAL(AG_ERR_TIMEOUT,
        ag_atecc_exec(AG_ATECC_OP_RANDOM, 0u, 0u, 0, 0u, out, 32u, 1u));
}

/* --------------------------------------------------------- the provider */

static void test_an_absent_element_reports_no_tier_and_refuses_everything(void)
{
    uint8_t out[32];
    uint8_t sig[64];
    uint8_t alg;

    /* ag_crypto_init is never called, so the provider has not found a part. */
    TEST_ASSERT_EQUAL(AG_CRYPTO_NONE, ag_crypto_tier());
    TEST_ASSERT_EQUAL(0u, ag_crypto_algorithms());
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, ag_crypto_random(out, 8u));
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, ag_crypto_sign(out, sig, &alg));
}

static void test_a_present_element_reports_the_secure_element_tier(void)
{
    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_CRYPTO_SECURE_ELEMENT, ag_crypto_tier());
}

/*
 * The algorithms reported are the ones the silicon has, and Ed25519 is not
 * among them. This part has no hardware Ed25519; advertising it would be the
 * same defect as a KMS backend claiming an algorithm it cannot perform, where
 * the host negotiates down to something the device then fails to do.
 */
static void test_the_element_does_not_claim_ed25519(void)
{
    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_BITS_HIGH(AG_ALGBIT_ECDSA_P256_SHA256, ag_crypto_algorithms());
    TEST_ASSERT_BITS_HIGH(AG_ALGBIT_HMAC_SHA256,       ag_crypto_algorithms());
    TEST_ASSERT_BITS_LOW(AG_ALGBIT_ED25519,            ag_crypto_algorithms());
}

/* An element with no UDS has no identity, whatever keys are in it. Signing
 * anyway would make "this device attested" mean "this device had a key". */
static void test_an_unprovisioned_element_refuses_to_sign(void)
{
    uint8_t digest[32] = { 0 };
    uint8_t sig[64];
    uint8_t alg = 0u;

    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());      /* present, no UDS */
    TEST_ASSERT_EQUAL(AG_CRYPTO_SECURE_ELEMENT, ag_crypto_tier());
    TEST_ASSERT_EQUAL(AG_ERR_NOT_PROVISIONED, ag_crypto_sign(digest, sig, &alg));
}

static void test_a_provisioned_element_signs_and_names_its_algorithm(void)
{
    uint8_t digest[32];
    uint8_t sig[64];
    uint8_t alg = 0u;
    uint8_t i;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)(0x30u + i); }
    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(AG_ALG_ECDSA_P256_SHA256, alg);
    /* The signature came back from the element, not from anything here. */
    TEST_ASSERT_EQUAL_HEX8(AG_ATECC_OP_SIGN, ag_model_last_opcode());
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

    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_FALSE(ag_model_is_awake());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, ag_model_tempkey(), 32u);
}

static void test_random_comes_from_the_element(void)
{
    uint8_t out[16];

    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_random(out, (uint8_t)sizeof out));
    TEST_ASSERT_EQUAL_HEX8(AG_ATECC_OP_RANDOM, ag_model_last_opcode());
}

static void test_random_refuses_a_length_it_cannot_fill(void)
{
    uint8_t out[64];

    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    /* One Random command yields 32 bytes. Asking for more must be refused
     * rather than answered with 32 good bytes and 32 stale ones. */
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ag_crypto_random(out, 33u));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ag_crypto_random(out, 0u));
}

static void test_the_boot_counter_is_read_from_the_element(void)
{
    uint32_t v = 0u;

    ag_model_provision();
    ag_model_set_counter(0x00ABCDEFuL);
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_counter(AG_ATECC_COUNTER_BOOT, &v));
    TEST_ASSERT_EQUAL_HEX32(0x00ABCDEFuL, v);
}

static void test_a_counter_the_part_does_not_have_is_refused(void)
{
    uint32_t v;

    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ag_crypto_counter(9u, &v));
}

/*
 * The element tier never signs with the CDI -- its DeviceID key is
 * element-resident, never derived (see the file header) -- but a CDI ag-boot
 * handed off must not sit resident in src/core/handoff.c for the process
 * lifetime regardless: ag_crypto_init drains and discards it. Provable from
 * outside that module: ag_boot_handoff_take_cdi returns 0 (nothing left to
 * take) after ag_crypto_init has run, whether or not the element itself is
 * even present.
 */
static void test_init_drains_a_handed_off_cdi_even_though_this_tier_has_no_use_for_it(void)
{
    uint8_t block[AG_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], measurement[32], taken[32];
    uint8_t i;

    for (i = 0u; i < sizeof cdi; i++)         { cdi[i] = (uint8_t)(0x11u + i); }
    for (i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    ag_boot_handoff_build(block, AG_CRYPTO_SECURE_ELEMENT, cdi, measurement, 1u, 0u);
    memcpy(ag_hal_handoff_block(), block, AG_BOOT_HANDOFF_LEN);
    ag_boot_handoff_init();

    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, ag_boot_handoff_take_cdi(taken));
}


/* ----------------------------------------------------------------- P-256 */

/* The whole path a verifier relies on: the key the driver reads back from the
 * element is the public half of the private key that signs, and a signature the
 * driver obtains verifies under it. */
static void test_a_signature_from_the_element_verifies_under_its_public_key(void)
{
    uint8_t digest[32], sig[64], pub[64], alg = 0u, i;

    for (i = 0u; i < 32u; i++) { digest[i] = (uint8_t)(0x30u + i); }
    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_sign(digest, sig, &alg));

    TEST_ASSERT_TRUE(ag_model_p256_verify(pub, digest, sig));
    digest[0] ^= 0x01u;
    TEST_ASSERT_FALSE(ag_model_p256_verify(pub, digest, sig));
}

static void test_the_public_key_read_from_the_element_matches_its_private_key(void)
{
    uint8_t pub[64], derived[64];

    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_pubkey(pub));
    TEST_ASSERT_TRUE(ag_model_p256_public(ag_model_deviceid_private(), derived));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(derived, pub, 64);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ag_model_deviceid_public(), pub, 64);
}

static void test_signing_a_different_digest_gives_a_different_signature(void)
{
    uint8_t a[32] = { 1 }, b[32] = { 2 }, sa[64], sb[64], alg;

    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_sign(a, sa, &alg));
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_sign(b, sb, &alg));
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
    TEST_ASSERT_TRUE(ag_model_p256_public(OP_PRIV, pub));
    TEST_ASSERT_TRUE(ag_model_p256_sign(OP_PRIV, digest, sig));
    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_verify_operator(digest, sig, pub));
    TEST_ASSERT_EQUAL_HEX8(AG_ATECC_OP_VERIFY, ag_model_last_opcode());
}

static void test_the_element_rejects_a_bad_operator_signature(void)
{
    uint8_t digest[32] = { 7 }, sig[64], pub[64], other[64], bad[64];

    TEST_ASSERT_TRUE(ag_model_p256_public(OP_PRIV, pub));
    TEST_ASSERT_TRUE(ag_model_p256_sign(OP_PRIV, digest, sig));
    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());

    /* Wrong digest. */
    digest[0] ^= 1u;
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, ag_crypto_verify_operator(digest, sig, pub));
    digest[0] ^= 1u;
    /* Corrupted signature. */
    memcpy(bad, sig, sizeof bad);
    bad[10] ^= 0x04u;
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, ag_crypto_verify_operator(digest, bad, pub));
    /* A valid signature, checked against somebody else's key. */
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_pubkey(other));
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, ag_crypto_verify_operator(digest, sig, other));
    /* A public key that is not a point on the curve. */
    memcpy(other, pub, sizeof other);
    other[40] ^= 0x01u;
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, ag_crypto_verify_operator(digest, sig, other));
    /* And the good one still passes: none of the above poisoned the element. */
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_verify_operator(digest, sig, pub));
}

/* GenKey in private mode makes a new key and returns only its public half; the
 * n-th key of a fresh part is always the same, so provisioning is reproducible. */
static void test_genkey_private_installs_a_new_key_and_returns_its_public_half(void)
{
    uint8_t pub[64], again[64], test_priv[32], test_pub[64];

    ag_model_provision();
    ag_model_test_deviceid(test_priv, test_pub);
    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_exec(AG_ATECC_OP_GENKEY, AG_ATECC_GENKEY_PRIVATE,
                                           AG_ATECC_SLOT_DEVICEID, 0, 0u, pub, 64u, 115u));
    (void)ag_atecc_sleep();

    TEST_ASSERT_FALSE(memcmp(pub, test_pub, 64u) == 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ag_model_deviceid_public(), pub, 64);
    TEST_ASSERT_TRUE(ag_model_p256_public(ag_model_deviceid_private(), again));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(again, pub, 64);

    ag_model_reset();
    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_exec(AG_ATECC_OP_GENKEY, AG_ATECC_GENKEY_PRIVATE,
                                           AG_ATECC_SLOT_DEVICEID, 0, 0u, again, 64u, 115u));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pub, again, 64);
}

static void test_genkey_public_on_a_part_with_no_key_is_an_error(void)
{
    uint8_t pub[64];

    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    TEST_ASSERT_NOT_EQUAL(AG_OK, ag_atecc_exec(AG_ATECC_OP_GENKEY, AG_ATECC_GENKEY_PUBLIC,
                                               AG_ATECC_SLOT_DEVICEID, 0, 0u, pub, 64u, 115u));
}

/* HMAC is a real HMAC-SHA256 keyed by the slot named, so the CDI the element
 * computes is the one a verifier holding the UDS would. */
static void test_hmac_is_hmac_sha256_keyed_by_the_named_slot(void)
{
    static const uint16_t lens[] = { 0u, 1u, 63u, 64u, 65u, 130u };
    uint8_t               msg[130], got[32], want[32];
    unsigned              n, i;

    for (i = 0u; i < sizeof msg; i++) { msg[i] = (uint8_t)(i * 7u + 3u); }
    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());

    for (n = 0u; n < sizeof lens / sizeof lens[0]; n++) {
        TEST_ASSERT_EQUAL(AG_OK, ag_crypto_hmac(AG_ATECC_SLOT_UDS, msg, lens[n], got));
        ag_hmac_sha256(ag_model_slot(AG_ATECC_SLOT_UDS), 32u, msg, lens[n], want);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    }
}

static void test_hmac_under_a_different_key_gives_a_different_answer(void)
{
    uint8_t msg[32] = { 9 }, uds_mac[32], other_mac[32];

    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_hmac(AG_ATECC_SLOT_UDS, msg, 32u, uds_mac));
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_hmac(2u, msg, 32u, other_mac));   /* an empty slot */
    TEST_ASSERT_FALSE(memcmp(uds_mac, other_mac, 32u) == 0);
}

static void test_hmac_on_an_unprovisioned_element_is_refused(void)
{
    uint8_t msg[8] = { 0 }, out[32];

    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_NOT_EQUAL(AG_OK, ag_crypto_hmac(AG_ATECC_SLOT_UDS, msg, 8u, out));
}

/* The part signs what Nonce loaded, and nothing was loaded after a sleep. */
static void test_sign_with_nothing_in_tempkey_is_an_execution_error(void)
{
    uint8_t sig[64];

    ag_model_provision();
    TEST_ASSERT_EQUAL(AG_OK, ag_atecc_wake());
    TEST_ASSERT_NOT_EQUAL(AG_OK, ag_atecc_exec(AG_ATECC_OP_SIGN, AG_ATECC_SIGN_EXTERNAL,
                                               AG_ATECC_SLOT_DEVICEID, 0, 0u, sig, 64u, 64u));
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
    return UNITY_END();
}
