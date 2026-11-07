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
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "tfw/hal.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "boot_platform.h"
#include "platform_id.h"
#include "handoff.h"
#include "sha256.h"

/* This suite's only OTHER HAL needs, beyond the element model (which is the
 * I2C HAL here) -- the same reason test_alias.c defines tfw_hal_millis. None
 * of CAPS/PING/SYS_INFO is exercised; sys.c still references them at link
 * time. */
tfw_ms_t     tfw_hal_millis(void)              { return 0u; }
uint32_t    tfw_hal_free_ram(void)            { return 0u; }
tfw_reset_cause_t tfw_hal_reset_cause(void)    { return TFW_RESET_UNKNOWN; }
uint8_t     tfw_hal_uart_tx_idle(void)        { return 1u; }
void        tfw_hal_reset(void)               {}

/* Sized to the whole part, not just the reserved region: the "a read above
 * the boundary is never gated" case reads past TFW_BOOT_EE_BLOCK_LEN on purpose. */
static uint8_t test_eeprom[TFW_EEPROM_SIZE];
static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];

/* This suite never populates a handoff block -- the element tier derives its
 * CDI itself and only drains this for zeroization hygiene (see provider.c).
 * All-zero is "no valid handoff", exactly as an absent bootloader reports. */
uint8_t *tfw_hal_handoff_block(void)
{
    return test_handoff;
}

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return TFW_ERR_RANGE; }
    memcpy(buf, &test_eeprom[addr], len);
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *data, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return TFW_ERR_RANGE; }
    memcpy(&test_eeprom[addr], data, len);
    return TFW_OK;
}

/* The platform block's store is the same array, as it is on AVR. */
/* Set by a test to make the platform block unreadable, as a failed NVM read
 * on the board would be. Cleared by setUp. */
static uint8_t nvm_read_fails;

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if (nvm_read_fails) { return TFW_ERR_RANGE; }
    return tfw_hal_eeprom_read(addr, buf, len);
}

static const uint8_t SERIAL[TFW_BOOT_SERIAL_LEN] = {
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11
};
/* The operator's key pair, made in setUp. The private half exists only in this test, as it
 * only ever exists in the operator's custody; the device holds the public
 * half, baked into the platform block. */
static uint8_t operator_priv[32];
/* A key the device has never been told about. */
static uint8_t stranger_priv[32];
static uint8_t stranger_pub[TFW_BOOT_OPERATOR_PUBKEY_LEN];
static uint8_t operator_pub[TFW_BOOT_OPERATOR_PUBKEY_LEN];

void setUp(void)
{
    tfw_boot_platform_t plat;

    nvm_read_fails = 0u;
    tfw_model_reset();
    tfw_model_provision();

    tfw_model_p256_key("operator", operator_priv, operator_pub);
    tfw_model_p256_key("stranger", stranger_priv, stranger_pub);
    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, SERIAL, sizeof SERIAL);
    memcpy(plat.operator_pubkey, operator_pub, sizeof operator_pub);
    memset(test_eeprom, 0, sizeof test_eeprom);
    tfw_boot_platform_serialise(&plat, test_eeprom);
    tfw_platform_id_init();

    memset(test_handoff, 0, sizeof test_handoff);
    tfw_boot_handoff_init();
    tfw_crypto_init();

    tfw_auth_session_init();
    tfw_sys_reset_state();
    tfw_sys_reset_state();
}
void tearDown(void) {}

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

static tfw_status_t challenge(uint8_t *nonce_out)
{
    tfw_status_t st;
    tfw_reader_init(&r, inbuf, 0u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    st = tfw_auth_cmd_challenge(&r, &w);
    if (st == TFW_OK && nonce_out) {
        memcpy(nonce_out, outbuf, TFW_AUTH_NONCE_LEN);
    }
    return st;
}

static tfw_status_t response(const uint8_t sig[TFW_AUTH_SIG_LEN])
{
    memcpy(inbuf, sig, TFW_AUTH_SIG_LEN);
    tfw_reader_init(&r, inbuf, TFW_AUTH_SIG_LEN);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_auth_cmd_response(&r, &w);
}

/* What the operator signs: SHA-256 of domain || serial || nonce, the same bytes
 * sdk/go's AuthResponse hashes. */
static void auth_digest(const uint8_t serial[TFW_BOOT_SERIAL_LEN],
                        const uint8_t nonce[TFW_AUTH_NONCE_LEN], uint8_t out[32])
{
    static const char domain[] = "tfw-auth-v1";
    uint8_t           msg[sizeof domain - 1u + TFW_BOOT_SERIAL_LEN + TFW_AUTH_NONCE_LEN];

    memcpy(msg, domain, sizeof domain - 1u);
    memcpy(msg + sizeof domain - 1u, serial, TFW_BOOT_SERIAL_LEN);
    memcpy(msg + sizeof domain - 1u + TFW_BOOT_SERIAL_LEN, nonce, TFW_AUTH_NONCE_LEN);
    tfw_sha256(msg, (uint16_t)sizeof msg, out);
}

/* The operator's signature over a challenge, made with the given private key. */
static void sign_challenge(const uint8_t priv[32], const uint8_t serial[TFW_BOOT_SERIAL_LEN],
                           const uint8_t nonce[TFW_AUTH_NONCE_LEN], uint8_t sig[TFW_AUTH_SIG_LEN])
{
    uint8_t digest[32];

    auth_digest(serial, nonce, digest);
    TEST_ASSERT_TRUE(tfw_model_p256_sign(priv, digest, sig));
}

/* Asks for a challenge and answers it with a signature from priv. */
static tfw_status_t authenticate_with(const uint8_t priv[32])
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN], sig[TFW_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, challenge(nonce));
    sign_challenge(priv, SERIAL, nonce, sig);
    return response(sig);
}

/* -------------------------------------------------------------- the gate */

static void test_the_gate_is_closed_until_a_session_is_granted(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

static void test_challenge_returns_a_nonce_of_the_wire_width(void)
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN];
    TEST_ASSERT_EQUAL(TFW_OK, challenge(nonce));
    TEST_ASSERT_EQUAL_UINT8(TFW_AUTH_NONCE_LEN, tfw_writer_len(&w));
}

/* auth_session.c:47-48: the element will not give a nonce, so no challenge is
 * left pending and a response to it is refused. */
static void test_a_challenge_with_no_element_is_a_crypto_error(void)
{
    uint8_t sig[TFW_AUTH_SIG_LEN];

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, challenge(NULL));
    tfw_model_set_absent(0u);
    memset(sig, 0, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, response(sig));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* platform_id.c:46: an unreadable platform block gives an all-zero operator
 * key, never a stale one. */
static void test_an_unreadable_platform_block_gives_a_zero_operator_key(void)
{
    uint8_t key[TFW_BOOT_OPERATOR_PUBKEY_LEN], zero[TFW_BOOT_OPERATOR_PUBKEY_LEN];

    memset(key, 0xA5, sizeof key);
    memset(zero, 0, sizeof zero);
    nvm_read_fails = 1u;
    tfw_platform_id_operator_pubkey(key);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, key, sizeof key);
}

/* ------------------------------------------------------------ happy path */

static void test_a_valid_signature_grants_the_session(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, authenticate_with(operator_priv));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
}

/* The driver must forward exactly the signature and public key it was given,
 * not something it constructed -- the element model records both. */
static void test_the_response_forwards_the_signature_and_the_baked_operator_key(void)
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN], sig[TFW_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, challenge(nonce));
    sign_challenge(operator_priv, SERIAL, nonce, sig);
    TEST_ASSERT_EQUAL(TFW_OK, response(sig));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sig, tfw_model_verify_last_sig(), TFW_AUTH_SIG_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(operator_pub, tfw_model_verify_last_pubkey(),
                                   TFW_BOOT_OPERATOR_PUBKEY_LEN);
}

/* ------------------------------------------------------------- the wrong signature */

/* A real signature, from a key the device does not trust. Valid ECDSA, wrong
 * signer: the element verifies against the baked key and says no. */
static void test_a_signature_from_another_key_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, authenticate_with(stranger_priv));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* One flipped bit in an otherwise correct signature. */
static void test_a_corrupted_signature_is_refused_and_leaves_the_gate_closed(void)
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN], sig[TFW_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, challenge(nonce));
    sign_challenge(operator_priv, SERIAL, nonce, sig);
    sig[40] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, response(sig));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* A signature the operator made for another device's serial. The message binds
 * the serial, so a capture from one device cannot open another. */
static void test_a_signature_made_for_another_device_is_refused(void)
{
    static const uint8_t other[TFW_BOOT_SERIAL_LEN] = {
        0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77
    };
    uint8_t nonce[TFW_AUTH_NONCE_LEN], sig[TFW_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, challenge(nonce));
    sign_challenge(operator_priv, other, nonce, sig);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, response(sig));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* A genuine signature over the PREVIOUS challenge, replayed against the new
 * one: correct key, correct serial, stale nonce. */
static void test_a_signature_over_an_earlier_challenge_is_refused(void)
{
    uint8_t first[TFW_AUTH_NONCE_LEN], second[TFW_AUTH_NONCE_LEN], sig[TFW_AUTH_SIG_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, challenge(first));
    sign_challenge(operator_priv, SERIAL, first, sig);
    TEST_ASSERT_EQUAL(TFW_OK, challenge(second));   /* the host moves on */
    /* The premise: the element's TRNG gave a different nonce. */
    TEST_ASSERT_FALSE(memcmp(first, second, TFW_AUTH_NONCE_LEN) == 0);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, response(sig));
}

/* -------------------------------------------------------- single-use / replay */

static void test_a_fresh_challenge_drops_an_existing_session(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, authenticate_with(operator_priv));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());

    TEST_ASSERT_EQUAL(TFW_OK, challenge(0));   /* asking again re-arms and revokes */
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* --------------------------------------------------- no challenge outstanding */

static void test_a_response_with_no_challenge_outstanding_is_refused(void)
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN] = { 0 }, sig[TFW_AUTH_SIG_LEN];

    sign_challenge(operator_priv, SERIAL, nonce, sig);   /* a good signature */
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, response(sig));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* --------------------------------------------- the commands it gates */

static tfw_status_t eeprom_read(uint16_t addr, uint8_t len)
{
    inbuf[0] = (uint8_t)(addr & 0xFFu);
    inbuf[1] = (uint8_t)(addr >> 8);
    inbuf[2] = len;
    tfw_reader_init(&r, inbuf, 3u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_storage_cmd_read(&r, &w);
}

static tfw_status_t reset_bootloader(void)
{
    inbuf[0] = 1u;   /* TFW_RESET_BOOTLOADER */
    tfw_reader_init(&r, inbuf, 1u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_sys_cmd_reset(&r, &w);
}

static void test_reserved_region_read_is_refused_before_authentication(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, eeprom_read(0u, 4u));
}

static void test_reserved_region_read_succeeds_after_authentication(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, authenticate_with(operator_priv));

    TEST_ASSERT_EQUAL(TFW_OK, eeprom_read(0u, 4u));
}

/* A read that STARTS above the boundary is never gated -- general-purpose
 * EEPROM, exactly as it always was. */
static void test_a_read_above_the_reserved_region_is_never_gated(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, eeprom_read(TFW_BOOT_EE_LEN, 4u));
}

/* RESET(1) enters the bootloader's update session, so on the element tier it
 * needs the operator session. Refused with nothing pending: a peer that cannot
 * authenticate must not be able to schedule the reset either. */
static void test_reset_mode_1_is_refused_without_a_session(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, reset_bootloader());
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());
}

static void test_reset_mode_1_is_accepted_with_a_session(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, authenticate_with(operator_priv));

    TEST_ASSERT_EQUAL(TFW_OK, reset_bootloader());
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
}

/* The range check survives: only modes 0 and 1 exist. */
static void test_reset_with_an_unknown_mode_is_refused(void)
{
    inbuf[0] = 2u;
    tfw_reader_init(&r, inbuf, 1u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_sys_cmd_reset(&r, &w));
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());
}

/* A plain reset (mode 0) is never gated. */
static void test_a_soft_reset_is_never_gated(void)
{
    inbuf[0] = 0u;
    tfw_reader_init(&r, inbuf, 1u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_reset(&r, &w));
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_gate_is_closed_until_a_session_is_granted);
    RUN_TEST(test_challenge_returns_a_nonce_of_the_wire_width);
    RUN_TEST(test_a_challenge_with_no_element_is_a_crypto_error);
    RUN_TEST(test_an_unreadable_platform_block_gives_a_zero_operator_key);
    RUN_TEST(test_a_valid_signature_grants_the_session);
    RUN_TEST(test_the_response_forwards_the_signature_and_the_baked_operator_key);
    RUN_TEST(test_a_signature_from_another_key_is_refused);
    RUN_TEST(test_a_corrupted_signature_is_refused_and_leaves_the_gate_closed);
    RUN_TEST(test_a_signature_made_for_another_device_is_refused);
    RUN_TEST(test_a_signature_over_an_earlier_challenge_is_refused);
    RUN_TEST(test_a_fresh_challenge_drops_an_existing_session);
    RUN_TEST(test_a_response_with_no_challenge_outstanding_is_refused);
    RUN_TEST(test_reserved_region_read_is_refused_before_authentication);
    RUN_TEST(test_reserved_region_read_succeeds_after_authentication);
    RUN_TEST(test_a_read_above_the_reserved_region_is_never_gated);
    RUN_TEST(test_reset_mode_1_is_refused_without_a_session);
    RUN_TEST(test_reset_mode_1_is_accepted_with_a_session);
    RUN_TEST(test_reset_with_an_unknown_mode_is_refused);
    RUN_TEST(test_a_soft_reset_is_never_gated);
    return UNITY_END();
}
