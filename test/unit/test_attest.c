/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Attestation, on the software tier.
 *
 * The interesting assertions here are the refusals. A device that cannot prove
 * what it is must say so, because the alternative -- evidence that looks
 * well-formed and means nothing -- is the failure a verifier cannot detect.
 */

#include <string.h>
#include "unity.h"
#include "attest.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "ag/crypto.h"
#include "ag/protocol.h"
#include "ag/config.h"
#include "uds_mock.h"

static uint8_t     inbuf[AG_MAX_PAYLOAD];
static uint8_t     outbuf[AG_MAX_PAYLOAD];
static ag_reader_t r;
static ag_writer_t w;

static const uint8_t TEST_UDS[32] = {
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF
};

void setUp(void)
{
    ag_mock_reset();
    ag_attest_reset_state();
    ag_mock_set_uds(0);            /* unprovisioned by default */
    ag_crypto_init();
}

static void provision(void)
{
    ag_mock_set_uds(TEST_UDS);
    ag_crypto_init();
}
void tearDown(void) {}

static ag_status_t run_cmd(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    ag_reader_init(&r, inbuf, n);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_dispatch(cmd, &r, &w);
}

static ag_status_t run(const uint8_t *p, uint8_t n)
{
    return run_cmd(AG_CMD_ATTEST, p, n);
}

static const uint8_t NONCE[AG_ATTEST_NONCE_LEN] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
};

/* The ENROLMENT challenge, which is 32 bytes and is not the 16 above. The two
 * widths are the contract; see AG_ALIAS_NONCE_LEN in include/ag/protocol.h. */
static const uint8_t CHALLENGE[AG_ALIAS_NONCE_LEN] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
    0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
};

/*
 * THE HEADLINE REFUSAL.
 *
 * This build carries the default all-zero device secret, so it has no identity.
 * It must say AG_ERR_NOT_PROVISIONED rather than sign with a key everyone
 * compiling this source would share -- which would be evidence that verifies
 * and proves nothing at all.
 */
static void test_an_unprovisioned_device_refuses_to_attest(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_NOT_PROVISIONED, run(NONCE, AG_ATTEST_NONCE_LEN));
}

/* The tier is reported honestly. A verifier learns what it is talking to
 * BEFORE it decides what the evidence is worth. */
static void test_the_software_tier_reports_itself_as_software(void)
{
    TEST_ASSERT_EQUAL(AG_CRYPTO_SOFTWARE, ag_crypto_tier());
}

/*
 * No AVR in this family has a TRNG, so the software tier refuses to produce
 * randomness rather than returning something that merely looks random. A
 * predictable nonce presented as a random one is worse than an error: the
 * error stops the protocol and the prediction does not.
 */
static void test_the_software_tier_refuses_to_invent_randomness(void)
{
    uint8_t buf[16];
    memset(buf, 0xAA, sizeof buf);
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, ag_crypto_random(buf, (uint8_t)sizeof buf));
    /* and it did not quietly fill the buffer anyway */
    for (uint8_t i = 0u; i < sizeof buf; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAAu, buf[i]);
    }
}

/* A symmetric identity has no public half. Saying so is the honest answer;
 * handing back the key would hand out the ability to forge. */
static void test_a_symmetric_identity_has_no_public_key(void)
{
    uint8_t pub[64];
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_crypto_pubkey(pub));
}

/* Without an element there is no monotonic counter, so the boot count is
 * unavailable rather than backed by a RAM counter that resets with the device
 * and would make every boot look like the first. */
static void test_there_is_no_monotonic_counter_without_an_element(void)
{
    uint32_t n;
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_crypto_counter(0u, &n));
}

/* A page request before any evidence exists would otherwise return zeros that
 * look like a valid answer. */
static void test_a_page_request_with_no_evidence_is_refused(void)
{
    const uint8_t page0[1] = { 0u };
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run(page0, 1u));
}

/* Arity: the command takes a 16-byte nonce or a 1-byte page and nothing else.
 * A nonce of the wrong length must not be padded or truncated into one. */
static void test_a_wrong_sized_argument_is_refused(void)
{
    const uint8_t short_nonce[8] = { 0 };
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run(short_nonce, 8u));
}

/* The evidence layout is a wire contract with the platform. Asserting the
 * constants here means a change to them is a deliberate act rather than a
 * silent incompatibility with every deployed verifier. */
static void test_the_evidence_layout_matches_the_specification(void)
{
    TEST_ASSERT_EQUAL_UINT8(164u, AG_ATTEST_LEN);
    TEST_ASSERT_EQUAL_UINT8(16u,  AG_ATTEST_NONCE_LEN);
    TEST_ASSERT_EQUAL_UINT8(1u,   AG_ATTEST_VERSION);
    /* the evidence does not fit one payload, which is why it is paged */
    TEST_ASSERT_GREATER_THAN_UINT8(AG_MAX_PAYLOAD, AG_ATTEST_LEN);
    /* and a page plus its two header bytes does fit */
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(AG_MAX_PAYLOAD, AG_ATTEST_PAGE + 2u);
}

/* ---------------------------------------------------------------------- */
/*  The positive path: a provisioned device produces evidence that binds the  */
/*  verifier's nonce.                                                        */
/* ---------------------------------------------------------------------- */

static void test_a_provisioned_device_produces_evidence(void)
{
    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ATTEST_NONCE_LEN));

    TEST_ASSERT_EQUAL_UINT8(0u,             outbuf[0]);   /* page */
    TEST_ASSERT_EQUAL_UINT8(AG_ATTEST_LEN,  outbuf[1]);   /* total */
    TEST_ASSERT_EQUAL_UINT8(AG_ATTEST_VERSION, outbuf[2]);
    /* the nonce is echoed at its specified offset */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NONCE, &outbuf[2 + 1], AG_ATTEST_NONCE_LEN);
}

/* The whole evidence must be retrievable, and the pages must join up. */
static void test_the_evidence_is_retrievable_in_pages(void)
{
    uint8_t whole[AG_ATTEST_LEN];
    uint8_t got = 0u;

    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ATTEST_NONCE_LEN));
    memcpy(whole, &outbuf[2], (size_t)(ag_writer_len(&w) - 2u));
    got = (uint8_t)(ag_writer_len(&w) - 2u);

    for (uint8_t page = 1u; got < AG_ATTEST_LEN; page++) {
        TEST_ASSERT_EQUAL(AG_OK, run(&page, 1u));
        TEST_ASSERT_EQUAL_UINT8(page,          outbuf[0]);
        TEST_ASSERT_EQUAL_UINT8(AG_ATTEST_LEN, outbuf[1]);
        memcpy(&whole[got], &outbuf[2], (size_t)(ag_writer_len(&w) - 2u));
        got = (uint8_t)(got + (ag_writer_len(&w) - 2u));
    }
    TEST_ASSERT_EQUAL_UINT8(AG_ATTEST_LEN, got);
    TEST_ASSERT_EQUAL_HEX8(AG_ALG_HMAC_SHA256, whole[73]);   /* OFF_ALG */
}

/*
 * THE PROPERTY THE WHOLE THING EXISTS FOR.
 *
 * Different challenge, different signature. If these matched, the evidence
 * would not be bound to the verifier's nonce and any recording of a previous
 * exchange could be replayed forever.
 */
static void test_a_different_nonce_produces_a_different_signature(void)
{
    uint8_t sig_a[32], sig_b[32];
    uint8_t other[AG_ATTEST_NONCE_LEN];

    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ATTEST_NONCE_LEN));
    /* the signature starts at offset 74; page 0 carries bytes 0..47, so read
     * it from the page that contains it */
    uint8_t page = 1u;
    TEST_ASSERT_EQUAL(AG_OK, run(&page, 1u));      /* bytes 48..95 */
    memcpy(sig_a, &outbuf[2 + (74u - 48u)], 22u);

    memcpy(other, NONCE, sizeof other);
    other[0] ^= 0xFFu;
    TEST_ASSERT_EQUAL(AG_OK, run(other, AG_ATTEST_NONCE_LEN));
    page = 1u;
    TEST_ASSERT_EQUAL(AG_OK, run(&page, 1u));
    memcpy(sig_b, &outbuf[2 + (74u - 48u)], 22u);

    TEST_ASSERT_FALSE(memcmp(sig_a, sig_b, 22u) == 0);
}

/* And the same challenge to a different DEVICE must not verify: the signature
 * depends on the device secret, not only on the nonce. */
static void test_a_different_device_secret_produces_a_different_signature(void)
{
    uint8_t sig_a[22], sig_b[22];
    uint8_t other_uds[32];
    uint8_t page;

    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ATTEST_NONCE_LEN));
    page = 1u; TEST_ASSERT_EQUAL(AG_OK, run(&page, 1u));
    memcpy(sig_a, &outbuf[2 + (74u - 48u)], sizeof sig_a);

    memcpy(other_uds, TEST_UDS, sizeof other_uds);
    other_uds[0] ^= 0xFFu;
    ag_mock_set_uds(other_uds);
    ag_crypto_init();
    ag_attest_reset_state();

    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ATTEST_NONCE_LEN));
    page = 1u; TEST_ASSERT_EQUAL(AG_OK, run(&page, 1u));
    memcpy(sig_b, &outbuf[2 + (74u - 48u)], sizeof sig_b);

    TEST_ASSERT_FALSE(memcmp(sig_a, sig_b, sizeof sig_a) == 0);
}

/* ---------------------------------------------------------------------- */
/*  The alias certificate, on a tier that cannot issue one.                  */
/* ---------------------------------------------------------------------- */

/*
 * A certificate names a subject PUBLIC key, and this tier's identity is an
 * HMAC. There is no public half, so there is nothing to certify -- and the
 * answer has to be AG_ERR_UNSUPPORTED rather than a certificate over a point
 * the firmware made up, or over the HMAC key itself, which would hand out the
 * ability to forge every response this device can produce.
 *
 * Distinct from AG_ERR_UNKNOWN_CMD on purpose. A build with the command
 * compiled out says the firmware has never heard of certificates; this says the
 * firmware knows exactly what one is and this root of trust cannot make it.
 */
static void test_the_software_tier_refuses_to_issue_an_alias_certificate(void)
{
    provision();
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED,
                      run_cmd(AG_CMD_ATTEST_CERT, CHALLENGE, AG_ALIAS_NONCE_LEN));
}

/* Refused before provisioning too, and for the earlier reason: there is no
 * identity at all. Either way nothing is issued. */
static void test_an_unprovisioned_device_refuses_an_alias_certificate(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED,
                      run_cmd(AG_CMD_ATTEST_CERT, CHALLENGE, AG_ALIAS_NONCE_LEN));
}

/* And a page request with nothing behind it is refused rather than answered
 * with bytes of a document that was never minted. */
static void test_an_alias_page_with_no_certificate_is_refused(void)
{
    const uint8_t page0[1] = { 0u };
    provision();
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run_cmd(AG_CMD_ATTEST_CERT, page0, 1u));
}

/* The two commands are separate ids answering separate questions, and the
 * dispatcher knows both. A host that finds one and not the other is talking to
 * a build that gated them apart, which is a supported configuration. */
static void test_both_attestation_commands_are_dispatchable(void)
{
    TEST_ASSERT_TRUE(ag_dispatch_knows(AG_CMD_ATTEST));
    TEST_ASSERT_TRUE(ag_dispatch_knows(AG_CMD_ATTEST_CERT));
    TEST_ASSERT_NOT_EQUAL(AG_CMD_ATTEST, AG_CMD_ATTEST_CERT);
}

/*
 * THE TWO NONCES ARE NOT INTERCHANGEABLE, and the dispatcher is where that is
 * enforced. ATTEST takes 16 bytes of runtime freshness; ATTEST_CERT takes a
 * 32-byte enrolment challenge. Feeding either command the other's width is
 * AG_ERR_BAD_ARGC, so a host that confused them finds out at the first
 * exchange rather than by producing evidence bound to a padded challenge.
 */
static void test_each_attestation_command_refuses_the_other_s_nonce(void)
{
    provision();
    TEST_ASSERT_NOT_EQUAL(AG_ATTEST_NONCE_LEN, AG_ALIAS_NONCE_LEN);
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC,
                      run_cmd(AG_CMD_ATTEST, CHALLENGE, AG_ALIAS_NONCE_LEN));
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC,
                      run_cmd(AG_CMD_ATTEST_CERT, NONCE, AG_ATTEST_NONCE_LEN));
}

static void test_a_page_past_the_end_is_refused(void)
{
    uint8_t page = 99u;
    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ATTEST_NONCE_LEN));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, run(&page, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_unprovisioned_device_refuses_to_attest);
    RUN_TEST(test_the_software_tier_reports_itself_as_software);
    RUN_TEST(test_the_software_tier_refuses_to_invent_randomness);
    RUN_TEST(test_a_symmetric_identity_has_no_public_key);
    RUN_TEST(test_there_is_no_monotonic_counter_without_an_element);
    RUN_TEST(test_a_page_request_with_no_evidence_is_refused);
    RUN_TEST(test_a_wrong_sized_argument_is_refused);
    RUN_TEST(test_the_evidence_layout_matches_the_specification);
    RUN_TEST(test_a_provisioned_device_produces_evidence);
    RUN_TEST(test_the_evidence_is_retrievable_in_pages);
    RUN_TEST(test_a_different_nonce_produces_a_different_signature);
    RUN_TEST(test_a_different_device_secret_produces_a_different_signature);
    RUN_TEST(test_a_page_past_the_end_is_refused);
    RUN_TEST(test_the_software_tier_refuses_to_issue_an_alias_certificate);
    RUN_TEST(test_an_unprovisioned_device_refuses_an_alias_certificate);
    RUN_TEST(test_an_alias_page_with_no_certificate_is_refused);
    RUN_TEST(test_both_attestation_commands_are_dispatchable);
    RUN_TEST(test_each_attestation_command_refuses_the_other_s_nonce);
    return UNITY_END();
}
