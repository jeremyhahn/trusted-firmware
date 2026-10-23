/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE THREE BEHAVIOURS THAT MATTER, against the real primitive.
 *
 *   a correctly signed image boots
 *   a tampered image refuses
 *   a downgrade is refused
 *
 * Nothing is stubbed out here except the hardware. The measurement is the real
 * SHA-256 over a real flash array; the signature is a real Ed25519 signature
 * produced by the same vendored ed25519-donna that verifies it, under a real
 * RFC 8032 key; the decision is the real tfw_boot_evaluate the device runs.
 *
 * The verify seam is supplied by this file rather than linked from
 * boot/verify/verify_ed25519_donna.c, because that unit also owns the public
 * key slot -- a const array in boot-section flash that a test cannot write --
 * and linking it would mean two definitions of tfw_boot_pubkey. Both functions
 * below are the shipping ones transcribed: tfw_boot_verify calls the same
 * ed25519_sign_open with the same inversion, and tfw_boot_pubkey replaces a
 * flash read rather than a decision. The shipping binding itself, including
 * what its unprovisioned slot does, is asserted in test_boot_ed25519.c.
 */

#include <string.h>
#include "unity.h"
#include "boot_policy.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_platform.h"
#include "boot_hal_mock.h"
#include "sha256.h"
#include "ed25519.h"

/* RFC 8032 section 7.1, TEST 2: a real keypair, so nothing here depends on a
 * key this test invented. */
static const char SECRET_HEX[] =
    "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb";

static uint8_t g_secret[32];
static uint8_t g_public[32];

/* Fault switches on the two seams this test supplies: what tfw_boot_pubkey
 * answers, and what tfw_boot_verify answers when handed no key (the element
 * tier). setUp puts both back to the soft tier's. */
static tfw_status_t g_pubkey_st;
static tfw_status_t g_stored_key_verdict;
static unsigned     g_verify_calls;
static unsigned     g_verify_null_key;

/* A settable key slot, standing in for the one in boot-section flash. */
tfw_status_t tfw_boot_pubkey(uint8_t out[TFW_BOOT_PUBKEY_LEN])
{
    if (g_pubkey_st != TFW_OK) {
        return g_pubkey_st;
    }
    memcpy(out, g_public, TFW_BOOT_PUBKEY_LEN);
    return TFW_OK;
}

/* The shipping binding, transcribed: donna returns 0 for a good signature. */
tfw_status_t tfw_boot_verify(const uint8_t *msg, uint16_t msglen,
                           const uint8_t sig[TFW_BOOT_SIG_LEN],
                           const uint8_t pub[TFW_BOOT_PUBKEY_LEN])
{
    g_verify_calls++;
    if (pub == 0) {
        /* The stored-key tier: the backend names its own key. This stand-in
         * has none, so it answers with the verdict the test set. */
        g_verify_null_key++;
        return g_stored_key_verdict;
    }
    if (ed25519_sign_open(msg, (size_t)msglen, pub, sig) != 0) {
        return TFW_ERR_CRYPTO;
    }
    return TFW_OK;
}

static uint8_t nib(char c)
{
    if (c >= '0' && c <= '9') { return (uint8_t)(c - '0'); }
    return (uint8_t)((c | 0x20) - 'a' + 10);
}

#define IMAGE_LEN 2048u

/*
 * Builds a complete, correctly signed image in the mock flash: body, trailer,
 * measurement, signature, and the pinned public-key hash in reserved EEPROM.
 *
 * This is the SIGNING TOOL, written out in a test, and that is the point --
 * running the two halves against each other is what proves the span the signer
 * covers is the span the bootloader verifies. A test that hand-wrote a trailer
 * would only prove the parser reads what the test wrote.
 */
static void install_bound_image(uint32_t version, uint8_t body_fill,
                                uint8_t board_id, uint8_t image_flags)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  raw[TFW_BOOT_TRAILER_LEN];
    uint8_t  ee[TFW_BOOT_EE_LEN];
    tfw_boot_trailer_t  tr;
    tfw_boot_platform_t plat;

    tfw_boot_mock_reset();

    for (uint16_t i = 0u; i < IMAGE_LEN; i++) {
        flash[i] = (uint8_t)(body_fill ^ (uint8_t)i);
    }

    memset(&tr, 0, sizeof tr);
    tr.image_version = version;
    tr.image_len     = IMAGE_LEN;
    tr.board_id      = board_id;
    tr.image_flags   = image_flags;
    tfw_sha256(flash, IMAGE_LEN, tr.measurement);

    /* The signature covers the trailer PREFIX -- magic, layout version, board
     * id, image flags, image version, image length and the measurement -- not
     * the measurement alone. Signing the measurement by itself would leave the
     * version unauthenticated, which test_a_relabelled_version_is_refused
     * exercises. */
    tfw_boot_trailer_serialise(&tr, raw);
    ed25519_sign(raw, TFW_BOOT_TR_SIGNED_LEN, g_secret, g_public,
                 &raw[TFW_BOOT_TR_SIGNATURE]);
    memcpy(flash + TFW_BOOT_TRAILER_ADDR, raw, TFW_BOOT_TRAILER_LEN);

    memset(ee, 0xFF, sizeof ee);
    memset(&plat, 0, sizeof plat);
    tfw_boot_hash(g_public, TFW_BOOT_PUBKEY_LEN, plat.pubkey_hash);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    tfw_boot_platform_serialise(&plat, ee);
    memcpy(tfw_boot_mock_nvm(), ee, TFW_BOOT_EE_LEN);
}

/* An image built for this board: the board profile's id, no flags. */
static void install_signed_image(uint32_t version, uint8_t body_fill)
{
    install_bound_image(version, body_fill, (uint8_t)TFW_BOARD_ID, 0u);
}

void setUp(void)
{
    for (size_t i = 0u; i < 32u; i++) {
        g_secret[i] = (uint8_t)((nib(SECRET_HEX[i * 2u]) << 4)
                                | nib(SECRET_HEX[i * 2u + 1u]));
    }
    ed25519_publickey(g_secret, g_public);
    g_pubkey_st          = TFW_OK;
    g_stored_key_verdict = TFW_OK;
    g_verify_calls       = 0u;
    g_verify_null_key    = 0u;
}

void tearDown(void) {}

/* ------------------------------------------------- 1. a signed image boots */

static void test_a_correctly_signed_image_is_accepted(void)
{
    tfw_boot_verdict_t v;

    install_signed_image(3u, 0x5Au);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT32(3u, v.trailer.image_version);
    TEST_ASSERT_EQUAL_UINT32(IMAGE_LEN, v.trailer.image_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v.trailer.measurement, v.measurement,
                                  TFW_BOOT_MEASURE_LEN);
}

/* The measurement handed on is the one a verifier recomputes, so it must be
 * the plain SHA-256 of the image body and nothing else. */
static void test_the_verdict_carries_the_fwhash_a_verifier_would_compute(void)
{
    tfw_boot_verdict_t v;
    uint8_t expect[32];

    install_signed_image(1u, 0x11u);
    tfw_sha256(tfw_boot_mock_flash(), IMAGE_LEN, expect);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, v.measurement, 32);
}

/* ---------------------------------------------- 2. a tampered image refuses */

static void test_a_tampered_image_is_refused(void)
{
    tfw_boot_verdict_t v;

    install_signed_image(3u, 0x5Au);
    tfw_boot_mock_flash()[1000] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
}

static void test_a_tamper_in_the_first_and_last_byte_is_refused(void)
{
    tfw_boot_verdict_t v;

    install_signed_image(3u, 0x5Au);
    tfw_boot_mock_flash()[0] ^= 0x80u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));

    install_signed_image(3u, 0x5Au);
    tfw_boot_mock_flash()[IMAGE_LEN - 1u] ^= 0x80u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
}

/* An attacker who edits the image AND the recorded measurement to match still
 * has no signature over the new measurement. This is the case a bootloader that
 * only compared the measurement to the trailer would accept. */
static void test_a_tampered_image_with_a_matching_measurement_is_refused(void)
{
    tfw_boot_verdict_t v;
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  meas[32];

    install_signed_image(3u, 0x5Au);
    flash[500] ^= 0xFFu;
    tfw_sha256(flash, IMAGE_LEN, meas);
    memcpy(flash + TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_MEASUREMENT, meas, 32);

    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
}

static void test_a_tampered_signature_is_refused(void)
{
    tfw_boot_verdict_t v;

    install_signed_image(3u, 0x5Au);
    tfw_boot_mock_flash()[TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_SIGNATURE] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
}

static void test_a_blank_application_region_is_refused(void)
{
    tfw_boot_verdict_t v;

    tfw_boot_mock_reset();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_evaluate(0u, &v));
}

/* ------------------------------------------------ 3. a downgrade is refused */

static void test_a_downgrade_is_refused(void)
{
    tfw_boot_verdict_t v;

    install_signed_image(2u, 0x5Au);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_evaluate(5u, &v));
}

static void test_the_current_version_still_boots(void)
{
    tfw_boot_verdict_t v;

    install_signed_image(5u, 0x5Au);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(5u, &v));
}

/*
 * THE REASON THE SIGNATURE COVERS THE VERSION.
 *
 * A genuine old image, relabelled with a high version so it walks past the
 * floor. If the signature covered only the measurement, this would verify --
 * anti-rollback defeated by a four-byte edit and no key at all.
 */
static void test_a_relabelled_version_is_refused(void)
{
    tfw_boot_verdict_t v;
    uint8_t *flash = tfw_boot_mock_flash();

    install_signed_image(2u, 0x5Au);
    flash[TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_IMAGE_VERSION] = 99u;

    /* It now passes the rollback check ... */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_rollback_permit(99u, 5u));
    /* ... and is still refused, because nobody signed version 99. */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(5u, &v));
}

/* A correctly signed image for another board. Every other check would pass;
 * the binding refuses it first, and the EEPROM the device boots from is the
 * EEPROM it had. */
static void test_a_signed_image_for_another_board_is_refused(void)
{
    tfw_boot_verdict_t v;
    uint8_t            ee[TFW_BOOT_EE_LEN];

    install_bound_image(3u, 0x5Au, (uint8_t)(TFW_BOARD_ID + 1u), 0u);
    memcpy(ee, tfw_boot_mock_nvm(), TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ee, tfw_boot_mock_nvm(), TFW_BOOT_EE_LEN);
}

/* board_id 0, signed: every image from before the field. Rebuilt, not booted. */
static void test_a_signed_unbound_image_is_refused(void)
{
    tfw_boot_verdict_t v;

    install_bound_image(3u, 0x5Au, 0u, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_evaluate(0u, &v));
}

/* A reserved flag, signed: refused. A development image is not refused here. */
static void test_a_signed_image_with_a_reserved_flag_is_refused(void)
{
    tfw_boot_verdict_t v;

    install_bound_image(3u, 0x5Au, (uint8_t)TFW_BOARD_ID, 0x80u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_evaluate(0u, &v));

    install_bound_image(3u, 0x5Au, (uint8_t)TFW_BOARD_ID, TFW_BOOT_IMAGE_FLAG_DEV);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_HEX8(TFW_BOOT_IMAGE_FLAG_DEV, v.trailer.image_flags);
}

/* The binding is checked before the floor: an image for another board below
 * the floor names the board, not the version. */
static void test_the_board_is_checked_before_the_floor(void)
{
    tfw_boot_verdict_t v;

    install_bound_image(2u, 0x5Au, (uint8_t)(TFW_BOARD_ID + 1u), 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_evaluate(5u, &v));
}

/* Another board's genuine image, relabelled with this board's id. It now
 * passes the binding, and is still refused, because the board id is inside
 * the signed span and nobody signed this one for this board. */
static void test_a_relabelled_board_id_is_refused(void)
{
    tfw_boot_verdict_t v;
    tfw_boot_trailer_t tr;
    uint8_t *flash = tfw_boot_mock_flash();

    install_bound_image(3u, 0x5Au, (uint8_t)(TFW_BOARD_ID + 1u), 0u);
    flash[TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_BOARD_ID] = (uint8_t)TFW_BOARD_ID;

    TEST_ASSERT_EQUAL_INT(TFW_OK,
        tfw_boot_trailer_parse(flash + TFW_BOOT_TRAILER_ADDR, &tr));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_binding_permit(&tr, (uint8_t)TFW_BOARD_ID));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
}

/* ------------------------------------------------------- the key pinning */

/*
 * Replacing the bootloader replaces the key it verifies against. The SHA-256 in
 * reserved EEPROM is what makes that swap detectable from inside the
 * bootloader; without it, a rewritten boot section would verify happily against
 * the attacker's key.
 */
static void test_a_public_key_the_device_does_not_pin_is_refused(void)
{
    tfw_boot_verdict_t v;
    uint8_t *ee = tfw_boot_mock_nvm();
    uint8_t  raw[TFW_BOOT_EE_LEN];

    install_signed_image(3u, 0x5Au);
    memcpy(raw, ee, TFW_BOOT_EE_LEN);
    raw[TFW_BOOT_EE_O_PUBKEY_HASH] ^= 0x01u;
    /* Rewrite the CRC, so this tests the PIN and not the corruption check. */
    {
        tfw_boot_platform_t p;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, tfw_boot_platform_parse(raw, &p));
        (void)tfw_boot_platform_parse(ee, &p);
        p.pubkey_hash[0] ^= 0x01u;
        tfw_boot_platform_serialise(&p, raw);
    }
    memcpy(ee, raw, TFW_BOOT_EE_LEN);

    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, tfw_boot_evaluate(0u, &v));
}

static void test_the_pin_check_accepts_the_matching_key(void)
{
    uint8_t pinned[32];
    uint8_t pub[32];

    tfw_boot_hash(g_public, TFW_BOOT_PUBKEY_LEN, pinned);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_check_pubkey_pin(pinned, pub));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_public, pub, 32);
}

/* boot_policy.c tfw_boot_check_pubkey_pin: a key seam that fails is passed on
 * unchanged, and tfw_boot_evaluate_at's else-branch returns it before verify. */
static void test_a_failing_key_seam_is_passed_on_and_nothing_is_verified(void)
{
    tfw_boot_verdict_t v;
    uint8_t pinned[TFW_BOOT_MEASURE_LEN];
    uint8_t pub[TFW_BOOT_PUBKEY_LEN];

    install_signed_image(3u, 0x5Au);
    g_pubkey_st = TFW_ERR_INTERNAL;

    memset(pinned, 0, sizeof pinned);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_boot_check_pubkey_pin(pinned, pub));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(0u, g_verify_calls);
}

/* The element tier: tfw_boot_pubkey answers UNSUPPORTED, the pin is skipped
 * and verify is handed NULL, and its verdict is the policy's. */
static void test_a_key_the_backend_keeps_is_verified_with_no_key(void)
{
    tfw_boot_verdict_t v;

    install_signed_image(3u, 0x5Au);
    g_pubkey_st = TFW_ERR_UNSUPPORTED;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(1u, g_verify_null_key);

    g_stored_key_verdict = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(2u, g_verify_null_key);
}

/* The pinned hash's EEPROM read fails: the policy returns the read's status
 * and verifies nothing. */
static void test_a_failed_pin_read_is_passed_on_and_nothing_is_verified(void)
{
    tfw_boot_verdict_t v;

    install_signed_image(3u, 0x5Au);
    tfw_boot_mock_fail_nvm_reads(1u);

    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(0u, g_verify_calls);
}

/* ------------------------------------------------------- the comparison */

/* Correctness only: a host test cannot observe timing, so nothing here pins
 * that tfw_boot_equal is constant time. */
static void test_equal_refuses_a_difference_in_the_first_or_last_byte(void)
{
    uint8_t a[32], b[32];

    memset(a, 0x33, sizeof a);
    memset(b, 0x33, sizeof b);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_equal(a, b, 32u));

    b[31] ^= 0x01u;                       /* a difference in the LAST byte */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_equal(a, b, 32u));

    memset(b, 0x33, sizeof b);
    b[0] ^= 0x01u;                        /* and in the first */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_equal(a, b, 32u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_correctly_signed_image_is_accepted);
    RUN_TEST(test_the_verdict_carries_the_fwhash_a_verifier_would_compute);
    RUN_TEST(test_a_tampered_image_is_refused);
    RUN_TEST(test_a_tamper_in_the_first_and_last_byte_is_refused);
    RUN_TEST(test_a_tampered_image_with_a_matching_measurement_is_refused);
    RUN_TEST(test_a_tampered_signature_is_refused);
    RUN_TEST(test_a_blank_application_region_is_refused);
    RUN_TEST(test_a_downgrade_is_refused);
    RUN_TEST(test_the_current_version_still_boots);
    RUN_TEST(test_a_relabelled_version_is_refused);
    RUN_TEST(test_a_signed_image_for_another_board_is_refused);
    RUN_TEST(test_a_signed_unbound_image_is_refused);
    RUN_TEST(test_a_signed_image_with_a_reserved_flag_is_refused);
    RUN_TEST(test_the_board_is_checked_before_the_floor);
    RUN_TEST(test_a_relabelled_board_id_is_refused);
    RUN_TEST(test_a_public_key_the_device_does_not_pin_is_refused);
    RUN_TEST(test_the_pin_check_accepts_the_matching_key);
    RUN_TEST(test_a_failing_key_seam_is_passed_on_and_nothing_is_verified);
    RUN_TEST(test_a_key_the_backend_keeps_is_verified_with_no_key);
    RUN_TEST(test_a_failed_pin_read_is_passed_on_and_nothing_is_verified);
    RUN_TEST(test_equal_refuses_a_difference_in_the_first_or_last_byte);
    return UNITY_END();
}
