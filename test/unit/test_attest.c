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
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "uds_mock.h"
#include "boot_soft_mock.h"
#include "handoff.h"
#include "platform_id.h"
#include "boot_platform.h"
#include "boot_measure.h"
#include "sha256.h"
#include "deviceid_p256.h"

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

static const uint8_t TEST_UDS[32] = {
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF
};

void setUp(void)
{
    tfw_mock_reset();
    tfw_boot_handoff_init();        /* no handoff left over from a previous test */
    tfw_attest_reset_state();
    tfw_mock_set_uds(0);            /* unprovisioned by default */
    tfw_crypto_init();
}

/* What a soft tfw-boot measured, for provision(): any non-zero value. */
static const uint8_t TEST_MEAS[32] = { 0x4D, 0x45, 0x41, 0x53 };

/* A provisioned soft device: tfw-boot held the UDS and handed over the CDI and
 * the DeviceID seed (#173). The application never holds the UDS. */
static void provision(void)
{
    tfw_mock_boot_soft(TEST_UDS, TEST_MEAS);
    tfw_crypto_init();
}

/* A UDS the application could read itself, with no seed from tfw-boot: the
 * case the handoff tests below prove is never used to sign. */
static void local_uds(void)
{
    tfw_mock_set_uds(TEST_UDS);
    tfw_crypto_init();
}

/* The seed a soft tfw-boot adds to a test's own block, for `uds`. */
static void seed_block(uint8_t *block, const uint8_t uds[32])
{
    uint8_t seed[TFW_BOOT_SEED_LEN];

    tfw_hmac_sha256(uds, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                    TFW_DEVICEID_LABEL_LEN, seed);
    tfw_boot_handoff_set_seed(block, seed);
}
void tearDown(void) {}

static tfw_status_t run_cmd(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t run(const uint8_t *p, uint8_t n)
{
    return run_cmd(TFW_CMD_ATTEST, p, n);
}

static const uint8_t NONCE[TFW_ATTEST_NONCE_LEN] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
};

/* The ENROLMENT challenge, which is 32 bytes and is not the 16 above. The two
 * widths are the contract; see TFW_ALIAS_NONCE_LEN in include/tfw/protocol.h. */
static const uint8_t CHALLENGE[TFW_ALIAS_NONCE_LEN] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
    0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
};

/*
 * THE HEADLINE REFUSAL.
 *
 * This build carries the default all-zero device secret, so it has no identity.
 * It must say TFW_ERR_NOT_PROVISIONED rather than sign with a key everyone
 * compiling this source would share -- which would be evidence that verifies
 * and proves nothing at all.
 */
static void test_an_unprovisioned_device_refuses_to_attest(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, run(NONCE, TFW_ATTEST_NONCE_LEN));
}

/* The tier is reported honestly. A verifier learns what it is talking to
 * BEFORE it decides what the evidence is worth. */
static void test_the_software_tier_reports_itself_as_software(void)
{
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
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
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_random(buf, (uint8_t)sizeof buf));
    /* and it did not quietly fill the buffer anyway */
    for (uint8_t i = 0u; i < sizeof buf; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAAu, buf[i]);
    }
}

/* The soft root's public half is its P-256 DeviceID, derived from the UDS
 * (#48); with no UDS there is none, and the answer says so. */
static void test_an_unprovisioned_soft_root_has_no_public_key(void)
{
    uint8_t pub[64];
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_pubkey(pub));
}

/* Without an element there is no monotonic counter, so the boot count is
 * unavailable rather than backed by a RAM counter that resets with the device
 * and would make every boot look like the first. */
static void test_there_is_no_monotonic_counter_without_an_element(void)
{
    uint32_t n;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_counter(0u, &n));
}

/* The soft tier holds no operator or image key to check a signature against, so it refuses
 * both verifies -- a TFW_OK here would be every operator and every image
 * accepted on a device that cannot tell them apart. */
static void test_the_software_tier_verifies_no_signature(void)
{
    uint8_t digest[32] = {0}, sig[64] = {0}, pub[64] = {0};
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_operator(digest, sig, pub));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_image(digest, sig));
}

/* A page request before any evidence exists would otherwise return zeros that
 * look like a valid answer. */
static void test_a_page_request_with_no_evidence_is_refused(void)
{
    const uint8_t page0[1] = { 0u };
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(page0, 1u));
}

/* Arity: the command takes a 16-byte nonce or a 1-byte page and nothing else.
 * A nonce of the wrong length must not be padded or truncated into one. */
static void test_a_wrong_sized_argument_is_refused(void)
{
    const uint8_t short_nonce[8] = { 0 };
    const uint8_t long_nonce[TFW_ATTEST_NONCE_LEN + 1u] = { 0 };
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(short_nonce, 8u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(long_nonce, (uint8_t)sizeof long_nonce));
}

/* The evidence layout is a wire contract with the platform. Asserting the
 * constants here means a change to them is a deliberate act rather than a
 * silent incompatibility with every deployed verifier. */
static void test_the_evidence_layout_matches_the_specification(void)
{
    TEST_ASSERT_EQUAL_UINT8(164u, TFW_ATTEST_LEN);
    TEST_ASSERT_EQUAL_UINT8(16u,  TFW_ATTEST_NONCE_LEN);
    TEST_ASSERT_EQUAL_UINT8(1u,   TFW_ATTEST_VERSION);
    /* the evidence does not fit one payload, which is why it is paged */
    TEST_ASSERT_GREATER_THAN_UINT8(TFW_MAX_PAYLOAD, TFW_ATTEST_LEN);
    /* and a page plus its two header bytes does fit */
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(TFW_MAX_PAYLOAD, TFW_ATTEST_PAGE + 2u);
}

/* ---------------------------------------------------------------------- */
/*  The positive path: a provisioned device produces evidence that binds the  */
/*  verifier's nonce.                                                        */
/* ---------------------------------------------------------------------- */

static void test_a_provisioned_device_produces_evidence(void)
{
    provision();
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN));

    TEST_ASSERT_EQUAL_UINT8(0u,             outbuf[0]);   /* page */
    TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN,  outbuf[1]);   /* total */
    TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_VERSION, outbuf[2]);
    /* the nonce is echoed at its specified offset */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NONCE, &outbuf[2 + 1], TFW_ATTEST_NONCE_LEN);
}

/* The whole evidence must be retrievable, and the pages must join up. */
static void test_the_evidence_is_retrievable_in_pages(void)
{
    uint8_t whole[TFW_ATTEST_LEN];
    uint8_t got = 0u;

    provision();
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN));
    memcpy(whole, &outbuf[2], (size_t)(tfw_writer_len(&w) - 2u));
    got = (uint8_t)(tfw_writer_len(&w) - 2u);

    for (uint8_t page = 1u; got < TFW_ATTEST_LEN; page++) {
        TEST_ASSERT_EQUAL(TFW_OK, run(&page, 1u));
        TEST_ASSERT_EQUAL_UINT8(page,          outbuf[0]);
        TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN, outbuf[1]);
        memcpy(&whole[got], &outbuf[2], (size_t)(tfw_writer_len(&w) - 2u));
        got = (uint8_t)(got + (tfw_writer_len(&w) - 2u));
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN, got);
    /* The soft root's ES256 names its root: 0x11, a wire literal (#130). */
    TEST_ASSERT_EQUAL_HEX8(0x11u, whole[73]);   /* OFF_ALG */
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
    uint8_t other[TFW_ATTEST_NONCE_LEN];

    provision();
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN));
    /* the signature starts at offset 74; page 0 carries bytes 0..47, so read
     * it from the page that contains it */
    uint8_t page = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, run(&page, 1u));      /* bytes 48..95 */
    memcpy(sig_a, &outbuf[2 + (74u - 48u)], 22u);

    memcpy(other, NONCE, sizeof other);
    other[0] ^= 0xFFu;
    TEST_ASSERT_EQUAL(TFW_OK, run(other, TFW_ATTEST_NONCE_LEN));
    page = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, run(&page, 1u));
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
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN));
    page = 1u; TEST_ASSERT_EQUAL(TFW_OK, run(&page, 1u));
    memcpy(sig_a, &outbuf[2 + (74u - 48u)], sizeof sig_a);

    memcpy(other_uds, TEST_UDS, sizeof other_uds);
    other_uds[0] ^= 0xFFu;
    tfw_mock_boot_soft(other_uds, TEST_MEAS);
    tfw_crypto_init();
    tfw_attest_reset_state();

    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN));
    page = 1u; TEST_ASSERT_EQUAL(TFW_OK, run(&page, 1u));
    memcpy(sig_b, &outbuf[2 + (74u - 48u)], sizeof sig_b);

    TEST_ASSERT_FALSE(memcmp(sig_a, sig_b, sizeof sig_a) == 0);
}

/* ---------------------------------------------------------------------- */
/*  The alias certificate on the software tier.                            */
/* ---------------------------------------------------------------------- */

/*
 * A certificate names a subject PUBLIC key. The soft root's is its P-256
 * DeviceID (#48), so the builder mints one where it is linked; with no UDS it
 * refuses with TFW_ERR_NOT_PROVISIONED, never a certificate over a point the
 * firmware made up. A soft image links no builder (mk/firmware.mk ALIAS_SRCS).
 */
static void test_the_software_tier_mints_an_alias_certificate_for_its_deviceid(void)
{
    provision();
    TEST_ASSERT_EQUAL(TFW_OK,
                      run_cmd(TFW_CMD_ATTEST_CERT, CHALLENGE, TFW_ALIAS_NONCE_LEN));
}

/* Refused before provisioning too, and for the same reason: the tier check
 * answers before any identity is looked at, so this is TFW_ERR_UNSUPPORTED and
 * not TFW_ERR_NOT_PROVISIONED. Either way nothing is issued. */
static void test_an_unprovisioned_device_refuses_an_alias_certificate(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED,
                      run_cmd(TFW_CMD_ATTEST_CERT, CHALLENGE, TFW_ALIAS_NONCE_LEN));
}

/* And a page request with nothing behind it is refused rather than answered
 * with bytes of a document that was never minted. */
static void test_an_alias_page_with_no_certificate_is_refused(void)
{
    const uint8_t page0[1] = { 0u };
    provision();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run_cmd(TFW_CMD_ATTEST_CERT, page0, 1u));
}

/* The two commands are separate ids answering separate questions, and the
 * dispatcher knows both. A host that finds one and not the other is talking to
 * a build that gated them apart, which is a supported configuration. */
static void test_both_attestation_commands_are_dispatchable(void)
{
    TEST_ASSERT_TRUE(tfw_dispatch_knows(TFW_CMD_ATTEST));
    TEST_ASSERT_TRUE(tfw_dispatch_knows(TFW_CMD_ATTEST_CERT));
    TEST_ASSERT_NOT_EQUAL(TFW_CMD_ATTEST, TFW_CMD_ATTEST_CERT);
}

/*
 * THE TWO NONCES ARE NOT INTERCHANGEABLE, and the dispatcher is where that is
 * enforced. ATTEST takes 16 bytes of runtime freshness; ATTEST_CERT takes a
 * 32-byte enrolment challenge. Feeding either command the other's width is
 * TFW_ERR_BAD_ARGC, so a host that confused them finds out at the first
 * exchange rather than by producing evidence bound to a padded challenge.
 */
static void test_each_attestation_command_refuses_the_other_s_nonce(void)
{
    provision();
    TEST_ASSERT_NOT_EQUAL(TFW_ATTEST_NONCE_LEN, TFW_ALIAS_NONCE_LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC,
                      run_cmd(TFW_CMD_ATTEST, CHALLENGE, TFW_ALIAS_NONCE_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC,
                      run_cmd(TFW_CMD_ATTEST_CERT, NONCE, TFW_ATTEST_NONCE_LEN));
}

static void test_a_page_past_the_end_is_refused(void)
{
    uint8_t page = 99u;
    provision();
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(&page, 1u));
}

/* ---------------------------------------------------------------------- */
/*  DeviceID and FWHash: from the platform block and the boot handoff --    */
/*  issue #6. Offsets are the evidence frame's documented layout            */
/*  ([Version:1][Nonce:16][DeviceID:16][FWHash:32]...),                    */
/*  docs/dice-attestation/architecture.md.                                 */
/* ---------------------------------------------------------------------- */

#define TEST_OFF_DEVICE_ID 17u
#define TEST_OFF_FW_HASH   33u

/* Reassembles the whole 164-byte evidence frame across pages -- the same
 * technique test_the_evidence_is_retrievable_in_pages uses inline, factored
 * out because the tests below need to read fields the earlier tests never
 * had to look at. */
static void full_evidence(uint8_t *whole)
{
    uint8_t got = 0u;
    uint8_t page;

    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN));
    memcpy(whole, &outbuf[2], (size_t)(tfw_writer_len(&w) - 2u));
    got = (uint8_t)(tfw_writer_len(&w) - 2u);

    for (page = 1u; got < TFW_ATTEST_LEN; page++) {
        TEST_ASSERT_EQUAL(TFW_OK, run(&page, 1u));
        memcpy(&whole[got], &outbuf[2], (size_t)(tfw_writer_len(&w) - 2u));
        got = (uint8_t)(got + (tfw_writer_len(&w) - 2u));
    }
}

static const uint8_t TEST_SERIAL[TFW_ATTEST_ID_LEN] = {
    0x53,0x4E,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x31,0x00,0x00,0x00,0x00
};

/*
 * A device with no platform block at all -- the factory-fresh case, before
 * tfwprov ever bakes a serial in. DeviceID must stay zero rather than
 * fabricated, exactly as it always has.
 */
static void test_device_id_is_zero_with_no_platform_block(void)
{
    uint8_t whole[TFW_ATTEST_LEN];

    provision();
    full_evidence(whole);
    for (uint8_t i = 0u; i < TFW_ATTEST_ID_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00u, whole[TEST_OFF_DEVICE_ID + i]);
    }
}

/*
 * THE REGRESSION THIS EXISTS TO CATCH. A platform block tfwprov baked in must
 * reach the evidence frame as DeviceID -- not stay zero because nothing wired
 * the accessor in. Fails on zero, which is the whole point of the test.
 */
static void test_a_baked_serial_reaches_device_id(void)
{
    tfw_boot_platform_t plat;
    uint8_t             raw[TFW_BOOT_EE_BLOCK_LEN];
    uint8_t             whole[TFW_ATTEST_LEN];

    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, TEST_SERIAL, sizeof plat.serial);
    memset(raw, 0, sizeof raw);
    tfw_boot_platform_serialise(&plat, raw);
    memcpy(tfw_mock_eeprom(), raw, TFW_BOOT_EE_BLOCK_LEN);
    tfw_platform_id_init();

    provision();
    full_evidence(whole);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST_SERIAL, &whole[TEST_OFF_DEVICE_ID],
                                  TFW_ATTEST_ID_LEN);
    /* And it did NOT come back all zero -- the failure mode a broken wire-up
     * would produce silently. */
    {
        uint8_t all_zero = 1u;
        for (uint8_t i = 0u; i < TFW_ATTEST_ID_LEN; i++) {
            if (whole[TEST_OFF_DEVICE_ID + i] != 0u) { all_zero = 0u; }
        }
        TEST_ASSERT_FALSE(all_zero);
    }
}

/* A corrupted platform block must not leak whatever EEPROM held -- refused
 * exactly as an unwritten one is, and DeviceID stays zero. */
static void test_a_corrupted_platform_block_leaves_device_id_zero(void)
{
    tfw_boot_platform_t plat;
    uint8_t             raw[TFW_BOOT_EE_BLOCK_LEN];
    uint8_t             whole[TFW_ATTEST_LEN];

    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, TEST_SERIAL, sizeof plat.serial);
    memset(raw, 0, sizeof raw);
    tfw_boot_platform_serialise(&plat, raw);
    raw[TFW_BOOT_EE_O_SERIAL] ^= 0xFFu;      /* CRC now wrong */
    memcpy(tfw_mock_eeprom(), raw, TFW_BOOT_EE_BLOCK_LEN);
    tfw_platform_id_init();

    provision();
    full_evidence(whole);
    for (uint8_t i = 0u; i < TFW_ATTEST_ID_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00u, whole[TEST_OFF_DEVICE_ID + i]);
    }
}

/* No bootloader ran (or a stock one did): ATTEST is refused, the case SPEC
 * row dice-attestation names behind a stock bootloader. */
static void test_no_handoff_is_not_provisioned_even_with_a_local_uds(void)
{
    /* No tfw-boot, or a stock bootloader: no seed, so no DeviceID key and no
     * evidence -- not evidence signed from a UDS the application read
     * itself (#173). On a device uds_flash.c answers NULL here too. */
    local_uds();
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, run(NONCE, TFW_ATTEST_NONCE_LEN));
}

/*
 * THE OTHER REGRESSION THIS EXISTS TO CATCH: the measurement tfw-boot verified
 * must reach the evidence frame as FWHash, spanning the page boundary at byte
 * 48 the same way the real field does. Fails on zero.
 *
 * The CDI here is a real (non-zero) value, deliberately NOT the all-zero
 * "unprovisioned" sentinel -- tfw_crypto_init now correctly refuses to
 * provision from a right-tier handoff carrying that sentinel (see
 * test_a_valid_handoff_with_the_unprovisioned_sentinel_cdi_refuses_even_
 * with_a_real_local_uds), which would make ATTEST itself refuse and this
 * test's own full_evidence() call fail before ever reaching the FWHash
 * assertion below. local_uds() is still called so this also proves the
 * handed-off CDI takes priority over an available local UDS, not just that
 * a device happens to be provisioned somehow.
 */
static void test_a_handed_off_measurement_reaches_fw_hash(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32];
    uint8_t measurement[TFW_SHA256_LEN];
    uint8_t whole[TFW_ATTEST_LEN];

    for (uint8_t i = 0u; i < sizeof cdi; i++) { cdi[i] = (uint8_t)(0xC0u + i); }
    for (uint8_t i = 0u; i < sizeof measurement; i++) {
        measurement[i] = (uint8_t)(0xE0u + i);
    }
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, measurement, 1u, 0u);
    seed_block(block, TEST_UDS);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();

    local_uds();
    full_evidence(whole);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(measurement, &whole[TEST_OFF_FW_HASH],
                                  TFW_SHA256_LEN);
    {
        uint8_t all_zero = 1u;
        for (uint8_t i = 0u; i < TFW_SHA256_LEN; i++) {
            if (whole[TEST_OFF_FW_HASH + i] != 0u) { all_zero = 0u; }
        }
        TEST_ASSERT_FALSE(all_zero);
    }
}

/*
 * The soft tier's own identity, straight from a handed-off CDI, with NO local
 * UDS provisioned at all. If tfw_crypto_init did not prefer the handoff this
 * would be TFW_ERR_NOT_PROVISIONED -- setUp leaves the UDS at its
 * unprovisioned default and nothing here calls provision().
 */
static void test_a_nonzero_handed_off_cdi_provisions_the_soft_tier_alone(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], measurement[32];
    uint8_t sig[TFW_SIG_LEN];
    uint8_t alg = TFW_ALG_NONE;
    uint8_t digest[TFW_SHA256_LEN];

    for (uint8_t i = 0u; i < sizeof cdi; i++)         { cdi[i] = (uint8_t)(0xD0u + i); }
    for (uint8_t i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, measurement, 1u, 0u);
    seed_block(block, TEST_UDS);    /* the DeviceID is the seed's; the CDI gates it */
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    memset(digest, 0x42, sizeof digest);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, alg);
}

/*
 * THE REGRESSION A CODEX REVIEW FOUND. A handoff naming
 * TFW_CRYPTO_SECURE_ELEMENT is correctly drained and discarded by the soft
 * tier -- but this device ALSO has a real local UDS, independently
 * provisioned (provision(), below, patches the mock's own placeholder).
 * Before this was fixed, the mismatch branch fell through to the local
 * fallback derivation, which signed successfully using this UDS and a
 * PLACEHOLDER measurement -- while FWHash (src/caps/attest.c, independent of
 * this provider) would have reported the REAL measurement tfw-boot verified.
 * A verifier would see a signature that does not correspond to the
 * measurement claimed alongside it: evidence that verifies against nothing,
 * reached by a different path than the one #7's tier check was written to
 * close. The device must stay unprovisioned instead.
 */
static void test_a_mismatched_tier_handoff_refuses_even_with_a_real_local_uds(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], measurement[32];
    uint8_t sig[TFW_SIG_LEN];
    uint8_t alg = TFW_ALG_NONE;
    uint8_t digest[TFW_SHA256_LEN];

    for (uint8_t i = 0u; i < sizeof cdi; i++)         { cdi[i] = (uint8_t)(0xE0u + i); }
    for (uint8_t i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    tfw_boot_handoff_build(block, TFW_CRYPTO_SECURE_ELEMENT, cdi, measurement, 1u, 0u);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();

    local_uds();    /* a REAL local UDS -- tfw_crypto_soft_uds() would succeed */

    memset(digest, 0x42, sizeof digest);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, digest, sizeof digest, sig));
}

/*
 * THE SAME MISBINDING, REACHED FROM THE OTHER DIRECTION. A handoff naming
 * TFW_CRYPTO_SOFTWARE -- the RIGHT tier -- but carrying tfw-boot's own
 * all-zero "unprovisioned" sentinel CDI (tfw-boot ran, verified a real image,
 * found no UDS to derive from). tfw_boot_handoff_take_cdi correctly refuses
 * to offer an all-zero CDI as usable (src/core/handoff.c), but before this
 * was fixed, tfw_crypto_init's right-tier branch only set `provisioned` on a
 * SUCCESSFUL take and otherwise fell through to the local fallback below it
 * -- which, on a device that ALSO has a real local UDS (provision(), below),
 * signed successfully with that local UDS and a PLACEHOLDER measurement,
 * while FWHash reported the REAL measurement this same valid handoff
 * carried. A valid handoff for this tier must be authoritative either way:
 * a usable CDI provisions, an unusable one refuses, and only the absence of
 * any valid handoff may fall back to the local derivation.
 */
static void test_a_valid_handoff_with_the_unprovisioned_sentinel_cdi_refuses_even_with_a_real_local_uds(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], measurement[32];
    uint8_t sig[TFW_SIG_LEN];
    uint8_t alg = TFW_ALG_NONE;
    uint8_t digest[TFW_SHA256_LEN];

    memset(cdi, 0, sizeof cdi);    /* tfw-boot's own "unprovisioned" sentinel */
    for (uint8_t i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, measurement, 1u, 0u);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();

    local_uds();    /* a REAL local UDS -- tfw_crypto_soft_uds() would succeed */

    memset(digest, 0x42, sizeof digest);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, digest, sizeof digest, sig));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_unprovisioned_device_refuses_to_attest);
    RUN_TEST(test_the_software_tier_reports_itself_as_software);
    RUN_TEST(test_the_software_tier_refuses_to_invent_randomness);
    RUN_TEST(test_an_unprovisioned_soft_root_has_no_public_key);
    RUN_TEST(test_there_is_no_monotonic_counter_without_an_element);
    RUN_TEST(test_the_software_tier_verifies_no_signature);
    RUN_TEST(test_a_page_request_with_no_evidence_is_refused);
    RUN_TEST(test_a_wrong_sized_argument_is_refused);
    RUN_TEST(test_the_evidence_layout_matches_the_specification);
    RUN_TEST(test_a_provisioned_device_produces_evidence);
    RUN_TEST(test_the_evidence_is_retrievable_in_pages);
    RUN_TEST(test_a_different_nonce_produces_a_different_signature);
    RUN_TEST(test_a_different_device_secret_produces_a_different_signature);
    RUN_TEST(test_a_page_past_the_end_is_refused);
    RUN_TEST(test_device_id_is_zero_with_no_platform_block);
    RUN_TEST(test_a_baked_serial_reaches_device_id);
    RUN_TEST(test_a_corrupted_platform_block_leaves_device_id_zero);
    RUN_TEST(test_no_handoff_is_not_provisioned_even_with_a_local_uds);
    RUN_TEST(test_a_handed_off_measurement_reaches_fw_hash);
    RUN_TEST(test_a_nonzero_handed_off_cdi_provisions_the_soft_tier_alone);
    RUN_TEST(test_a_mismatched_tier_handoff_refuses_even_with_a_real_local_uds);
    RUN_TEST(test_a_valid_handoff_with_the_unprovisioned_sentinel_cdi_refuses_even_with_a_real_local_uds);
    RUN_TEST(test_the_software_tier_mints_an_alias_certificate_for_its_deviceid);
    RUN_TEST(test_an_unprovisioned_device_refuses_an_alias_certificate);
    RUN_TEST(test_an_alias_page_with_no_certificate_is_refused);
    RUN_TEST(test_both_attestation_commands_are_dispatchable);
    RUN_TEST(test_each_attestation_command_refuses_the_other_s_nonce);
    return UNITY_END();
}
