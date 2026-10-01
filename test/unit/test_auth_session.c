/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The operator-signature auth session, driven against the software element --
 * issue #4. The cases are mostly about the STATE MACHINE (is a challenge
 * outstanding, has it been consumed, is a session granted), and the element's
 * Verify is now real ECDSA P-256: the test plays the operator, holding a real
 * private key, and signs the same message the Go SDK's AuthResponse signs
 * (domain || serial || nonce). So "a wrong signature is refused" means a
 * signature that does not verify, not a flag a test flipped.
 */
#include <string.h>
#include "unity.h"
#include "auth_session.h"
#include "storage.h"
#include "sys.h"
#include "atecc608a_model.h"
#include "ag/config.h"
#include "ag/protocol.h"
#include "ag/hal.h"
#include "ag/crypto.h"
#include "ag/boot.h"
#include "boot_platform.h"
#include "platform_id.h"
#include "handoff.h"
#include "sha256.h"

/* This suite's only OTHER HAL needs, beyond the element model (which is the
 * I2C HAL here) -- the same reason test_alias.c defines ag_hal_millis. None
 * of CAPS/PING/SYS_INFO is exercised; sys.c still references them at link
 * time. */
ag_ms_t     ag_hal_millis(void)              { return 0u; }
uint16_t    ag_hal_free_ram(void)            { return 0u; }
ag_reset_cause_t ag_hal_reset_cause(void)    { return AG_RESET_UNKNOWN; }
uint8_t     ag_hal_uart_tx_idle(void)        { return 1u; }
void        ag_hal_reset(void)               {}

/* Sized to the whole part, not just the reserved region: the "a read above
 * the boundary is never gated" case reads past AG_BOOT_EE_LEN on purpose. */
static uint8_t test_eeprom[AG_EEPROM_SIZE];
static uint8_t test_handoff[AG_BOOT_HANDOFF_LEN];

/* This suite never populates a handoff block -- the element tier derives its
 * CDI itself and only drains this for zeroization hygiene (see provider.c).
 * All-zero is "no valid handoff", exactly as an absent bootloader reports. */
uint8_t *ag_hal_handoff_block(void)
{
    return test_handoff;
}

ag_status_t ag_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return AG_ERR_RANGE; }
    memcpy(buf, &test_eeprom[addr], len);
    return AG_OK;
}

ag_status_t ag_hal_eeprom_write(uint16_t addr, const uint8_t *data, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return AG_ERR_RANGE; }
    memcpy(&test_eeprom[addr], data, len);
    return AG_OK;
}

static const uint8_t SERIAL[AG_BOOT_SERIAL_LEN] = {
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11
};
/* The operator's key pair. The private half exists only in this test, as it
 * only ever exists in the operator's custody; the device holds the public
 * half, baked into the platform block. */
static const uint8_t OPERATOR_PRIV[32] = {
    0x4F,0x0C,0x91,0x37,0xA2,0x5E,0x68,0xD3,0x1B,0x74,0xC0,0x29,0xE8,0x56,0x3D,0xAA,
    0x02,0x9B,0x61,0xF4,0x8E,0x17,0xC5,0x73,0xDA,0x40,0x2C,0xB9,0x85,0x1E,0x6F,0x30
};
/* A key the device has never been told about. */
static const uint8_t STRANGER_PRIV[32] = {
    0x91,0x3A,0x5C,0xE7,0x08,0xB2,0x6D,0x44,0xF1,0x27,0x9E,0x80,0x35,0xCB,0x1A,0x62,
    0x7D,0x0F,0xA9,0x53,0xE4,0x16,0xBC,0x29,0x70,0x8A,0x3E,0xD5,0x41,0xF8,0x0B,0x96
};
static uint8_t operator_pub[AG_BOOT_OPERATOR_PUBKEY_LEN];

void setUp(void)
{
    ag_boot_platform_t plat;

    ag_model_reset();
    ag_model_provision();

    TEST_ASSERT_TRUE(ag_model_p256_public(OPERATOR_PRIV, operator_pub));
    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, SERIAL, sizeof SERIAL);
    memcpy(plat.operator_pubkey, operator_pub, sizeof operator_pub);
    memset(test_eeprom, 0, sizeof test_eeprom);
    ag_boot_platform_serialise(&plat, test_eeprom);
    ag_platform_id_init();

    memset(test_handoff, 0, sizeof test_handoff);
    ag_boot_handoff_init();
    ag_crypto_init();

    ag_auth_session_init();
    ag_sys_reset_state();
    ag_sys_reset_state();
}
void tearDown(void) {}

static uint8_t     inbuf[AG_MAX_PAYLOAD];
static uint8_t     outbuf[AG_MAX_PAYLOAD];
static ag_reader_t r;
static ag_writer_t w;

static ag_status_t challenge(uint8_t *nonce_out)
{
    ag_status_t st;
    ag_reader_init(&r, inbuf, 0u);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    st = ag_auth_cmd_challenge(&r, &w);
    if (st == AG_OK && nonce_out) {
        memcpy(nonce_out, outbuf, AG_AUTH_NONCE_LEN);
    }
    return st;
}

static ag_status_t response(const uint8_t sig[AG_AUTH_SIG_LEN])
{
    memcpy(inbuf, sig, AG_AUTH_SIG_LEN);
    ag_reader_init(&r, inbuf, AG_AUTH_SIG_LEN);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_auth_cmd_response(&r, &w);
}

/* What the operator signs: SHA-256 of domain || serial || nonce, the same bytes
 * sdk/go's AuthResponse hashes. */
static void auth_digest(const uint8_t serial[AG_BOOT_SERIAL_LEN],
                        const uint8_t nonce[AG_AUTH_NONCE_LEN], uint8_t out[32])
{
    static const char domain[] = "ag-auth-v1";
    uint8_t           msg[sizeof domain - 1u + AG_BOOT_SERIAL_LEN + AG_AUTH_NONCE_LEN];

    memcpy(msg, domain, sizeof domain - 1u);
    memcpy(msg + sizeof domain - 1u, serial, AG_BOOT_SERIAL_LEN);
    memcpy(msg + sizeof domain - 1u + AG_BOOT_SERIAL_LEN, nonce, AG_AUTH_NONCE_LEN);
    ag_sha256(msg, (uint16_t)sizeof msg, out);
}

/* The operator's signature over a challenge, made with the given private key. */
static void sign_challenge(const uint8_t priv[32], const uint8_t serial[AG_BOOT_SERIAL_LEN],
                           const uint8_t nonce[AG_AUTH_NONCE_LEN], uint8_t sig[AG_AUTH_SIG_LEN])
{
    uint8_t digest[32];

    auth_digest(serial, nonce, digest);
    TEST_ASSERT_TRUE(ag_model_p256_sign(priv, digest, sig));
}

/* Asks for a challenge and answers it with a signature from priv. */
static ag_status_t authenticate_with(const uint8_t priv[32])
{
    uint8_t nonce[AG_AUTH_NONCE_LEN], sig[AG_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(AG_OK, challenge(nonce));
    sign_challenge(priv, SERIAL, nonce, sig);
    return response(sig);
}

/* -------------------------------------------------------------- the gate */

static void test_the_gate_is_closed_until_a_session_is_granted(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, ag_auth_gate());
}

static void test_challenge_returns_a_nonce_of_the_wire_width(void)
{
    uint8_t nonce[AG_AUTH_NONCE_LEN];
    TEST_ASSERT_EQUAL(AG_OK, challenge(nonce));
}

/* ------------------------------------------------------------ happy path */

static void test_a_valid_signature_grants_the_session(void)
{
    TEST_ASSERT_EQUAL(AG_OK, authenticate_with(OPERATOR_PRIV));
    TEST_ASSERT_EQUAL(AG_OK, ag_auth_gate());
}

/* The driver must forward exactly the signature and public key it was given,
 * not something it constructed -- the element model records both. */
static void test_the_response_forwards_the_signature_and_the_baked_operator_key(void)
{
    uint8_t nonce[AG_AUTH_NONCE_LEN], sig[AG_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(AG_OK, challenge(nonce));
    sign_challenge(OPERATOR_PRIV, SERIAL, nonce, sig);
    TEST_ASSERT_EQUAL(AG_OK, response(sig));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sig, ag_model_verify_last_sig(), AG_AUTH_SIG_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(operator_pub, ag_model_verify_last_pubkey(),
                                   AG_BOOT_OPERATOR_PUBKEY_LEN);
}

/* ------------------------------------------------------------- the wrong signature */

/* A real signature, from a key the device does not trust. Valid ECDSA, wrong
 * signer: the element verifies against the baked key and says no. */
static void test_a_signature_from_another_key_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, authenticate_with(STRANGER_PRIV));
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, ag_auth_gate());
}

/* One flipped bit in an otherwise correct signature. */
static void test_a_corrupted_signature_is_refused_and_leaves_the_gate_closed(void)
{
    uint8_t nonce[AG_AUTH_NONCE_LEN], sig[AG_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(AG_OK, challenge(nonce));
    sign_challenge(OPERATOR_PRIV, SERIAL, nonce, sig);
    sig[40] ^= 0x01u;
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, response(sig));
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, ag_auth_gate());
}

/* A signature the operator made for another device's serial. The message binds
 * the serial, so a capture from one device cannot open another. */
static void test_a_signature_made_for_another_device_is_refused(void)
{
    static const uint8_t other[AG_BOOT_SERIAL_LEN] = {
        0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77
    };
    uint8_t nonce[AG_AUTH_NONCE_LEN], sig[AG_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(AG_OK, challenge(nonce));
    sign_challenge(OPERATOR_PRIV, other, nonce, sig);
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, response(sig));
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, ag_auth_gate());
}

/* A genuine signature over the PREVIOUS challenge, replayed against the new
 * one: correct key, correct serial, stale nonce. */
static void test_a_signature_over_an_earlier_challenge_is_refused(void)
{
    uint8_t first[AG_AUTH_NONCE_LEN], second[AG_AUTH_NONCE_LEN], sig[AG_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(AG_OK, challenge(first));
    sign_challenge(OPERATOR_PRIV, SERIAL, first, sig);
    TEST_ASSERT_EQUAL(AG_OK, challenge(second));   /* the host moves on */
    /* The premise: the element's TRNG gave a different nonce. */
    TEST_ASSERT_FALSE(memcmp(first, second, AG_AUTH_NONCE_LEN) == 0);
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, response(sig));
}

/* -------------------------------------------------------- single-use / replay */

static void test_a_second_response_to_the_same_challenge_is_refused(void)
{
    uint8_t nonce[AG_AUTH_NONCE_LEN], sig[AG_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(AG_OK, challenge(nonce));
    sign_challenge(OPERATOR_PRIV, SERIAL, nonce, sig);
    TEST_ASSERT_EQUAL(AG_OK, response(sig));

    /* The challenge is single-use: a second AUTH_RESPONSE against the SAME
     * (already-consumed) challenge must be refused even though the signature
     * is still a good one -- there is no outstanding challenge left for it to
     * answer. */
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, response(sig));
}

static void test_a_fresh_challenge_drops_an_existing_session(void)
{
    TEST_ASSERT_EQUAL(AG_OK, authenticate_with(OPERATOR_PRIV));
    TEST_ASSERT_EQUAL(AG_OK, ag_auth_gate());

    TEST_ASSERT_EQUAL(AG_OK, challenge(0));   /* asking again re-arms and revokes */
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, ag_auth_gate());
}

/* --------------------------------------------------- no challenge outstanding */

static void test_a_response_with_no_challenge_outstanding_is_refused(void)
{
    uint8_t nonce[AG_AUTH_NONCE_LEN] = { 0 }, sig[AG_AUTH_SIG_LEN];

    sign_challenge(OPERATOR_PRIV, SERIAL, nonce, sig);   /* a good signature */
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, response(sig));
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, ag_auth_gate());
}

/* ------------------------------------------------------------- bad lengths */

static void test_a_short_response_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_OK, challenge(0));
    ag_reader_init(&r, inbuf, AG_AUTH_SIG_LEN - 1u);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, ag_auth_cmd_response(&r, &w));
}

/* --------------------------------------------- the commands it gates */

static ag_status_t eeprom_read(uint16_t addr, uint8_t len)
{
    inbuf[0] = (uint8_t)(addr & 0xFFu);
    inbuf[1] = (uint8_t)(addr >> 8);
    inbuf[2] = len;
    ag_reader_init(&r, inbuf, 3u);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_storage_cmd_read(&r, &w);
}

static ag_status_t reset_bootloader(void)
{
    inbuf[0] = 1u;   /* AG_RESET_BOOTLOADER */
    ag_reader_init(&r, inbuf, 1u);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_sys_cmd_reset(&r, &w);
}

static void test_reserved_region_read_is_refused_before_authentication(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, eeprom_read(0u, 4u));
}

static void test_reserved_region_read_succeeds_after_authentication(void)
{
    TEST_ASSERT_EQUAL(AG_OK, authenticate_with(OPERATOR_PRIV));

    TEST_ASSERT_EQUAL(AG_OK, eeprom_read(0u, 4u));
}

/* A read that STARTS above the boundary is never gated -- general-purpose
 * EEPROM, exactly as it always was. */
static void test_a_read_above_the_reserved_region_is_never_gated(void)
{
    TEST_ASSERT_EQUAL(AG_OK, eeprom_read(AG_BOOT_EE_LEN, 4u));
}

/* RESET(1) enters the bootloader's update session, so on the element tier it
 * needs the operator session. Refused with nothing pending: a peer that cannot
 * authenticate must not be able to schedule the reset either. */
static void test_reset_mode_1_is_refused_without_a_session(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, reset_bootloader());
    TEST_ASSERT_FALSE(ag_sys_reset_pending());
}

static void test_reset_mode_1_is_accepted_with_a_session(void)
{
    TEST_ASSERT_EQUAL(AG_OK, authenticate_with(OPERATOR_PRIV));

    TEST_ASSERT_EQUAL(AG_OK, reset_bootloader());
    TEST_ASSERT_TRUE(ag_sys_reset_pending());
}

/* A failed AUTH_RESPONSE leaves no session, so it opens nothing. */
static void test_reset_mode_1_is_refused_after_a_failed_authentication(void)
{
    TEST_ASSERT_NOT_EQUAL(AG_OK, authenticate_with(STRANGER_PRIV));

    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, reset_bootloader());
    TEST_ASSERT_FALSE(ag_sys_reset_pending());
}

/* The range check survives: only modes 0 and 1 exist. */
static void test_reset_with_an_unknown_mode_is_refused(void)
{
    inbuf[0] = 2u;
    ag_reader_init(&r, inbuf, 1u);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ag_sys_cmd_reset(&r, &w));
    TEST_ASSERT_FALSE(ag_sys_reset_pending());
}

/* A plain reset (mode 0) is never gated. */
static void test_a_soft_reset_is_never_gated(void)
{
    inbuf[0] = 0u;
    ag_reader_init(&r, inbuf, 1u);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL(AG_OK, ag_sys_cmd_reset(&r, &w));
    TEST_ASSERT_TRUE(ag_sys_reset_pending());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_gate_is_closed_until_a_session_is_granted);
    RUN_TEST(test_challenge_returns_a_nonce_of_the_wire_width);
    RUN_TEST(test_a_valid_signature_grants_the_session);
    RUN_TEST(test_the_response_forwards_the_signature_and_the_baked_operator_key);
    RUN_TEST(test_a_signature_from_another_key_is_refused);
    RUN_TEST(test_a_corrupted_signature_is_refused_and_leaves_the_gate_closed);
    RUN_TEST(test_a_signature_made_for_another_device_is_refused);
    RUN_TEST(test_a_signature_over_an_earlier_challenge_is_refused);
    RUN_TEST(test_a_second_response_to_the_same_challenge_is_refused);
    RUN_TEST(test_a_fresh_challenge_drops_an_existing_session);
    RUN_TEST(test_a_response_with_no_challenge_outstanding_is_refused);
    RUN_TEST(test_a_short_response_is_refused);
    RUN_TEST(test_reserved_region_read_is_refused_before_authentication);
    RUN_TEST(test_reserved_region_read_succeeds_after_authentication);
    RUN_TEST(test_a_read_above_the_reserved_region_is_never_gated);
    RUN_TEST(test_reset_mode_1_is_refused_without_a_session);
    RUN_TEST(test_reset_mode_1_is_accepted_with_a_session);
    RUN_TEST(test_reset_mode_1_is_refused_after_a_failed_authentication);
    RUN_TEST(test_reset_with_an_unknown_mode_is_refused);
    RUN_TEST(test_a_soft_reset_is_never_gated);
    return UNITY_END();
}
