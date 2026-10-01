/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The operator-signature auth session, driven against the software element --
 * issue #4. Every interesting case here is about the STATE MACHINE (is a
 * challenge outstanding, has it been consumed, is a session granted), not
 * about ECDSA: the element model answers Verify from a flag a test sets
 * (ag_model_set_verify_result), exactly as test_alias.c's certificate suite
 * already does for the same primitive, and for the same reason -- modelling
 * the curve here would test the model, not the driver or this state machine.
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
static const uint8_t OPERATOR_PUBKEY[AG_BOOT_OPERATOR_PUBKEY_LEN] = {
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22
};

void setUp(void)
{
    ag_boot_platform_t plat;

    ag_model_reset();
    ag_model_provision();

    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, SERIAL, sizeof SERIAL);
    memcpy(plat.operator_pubkey, OPERATOR_PUBKEY, sizeof OPERATOR_PUBKEY);
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

static const uint8_t ARBITRARY_SIG[AG_AUTH_SIG_LEN] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
    17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
    33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,
    49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64
};

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
    TEST_ASSERT_EQUAL(AG_OK, challenge(0));
    ag_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(AG_OK, response(ARBITRARY_SIG));
    TEST_ASSERT_EQUAL(AG_OK, ag_auth_gate());
}

/* The driver must forward exactly the signature and public key it was given,
 * not something it constructed -- the element model records both. */
static void test_the_response_forwards_the_signature_and_the_baked_operator_key(void)
{
    TEST_ASSERT_EQUAL(AG_OK, challenge(0));
    ag_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(AG_OK, response(ARBITRARY_SIG));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ARBITRARY_SIG, ag_model_verify_last_sig(), AG_AUTH_SIG_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(OPERATOR_PUBKEY, ag_model_verify_last_pubkey(),
                                   AG_BOOT_OPERATOR_PUBKEY_LEN);
}

/* ------------------------------------------------------------- the wrong signature */

static void test_a_wrong_signature_is_refused_and_leaves_the_gate_closed(void)
{
    TEST_ASSERT_EQUAL(AG_OK, challenge(0));
    ag_model_set_verify_result(0u);
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, response(ARBITRARY_SIG));
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, ag_auth_gate());
}

/* -------------------------------------------------------- single-use / replay */

static void test_a_second_response_to_the_same_challenge_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_OK, challenge(0));
    ag_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(AG_OK, response(ARBITRARY_SIG));

    /* The challenge is single-use: a second AUTH_RESPONSE against the SAME
     * (already-consumed) challenge must be refused even though the element
     * would still say the signature is good -- there is no outstanding
     * challenge left for it to answer. */
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, response(ARBITRARY_SIG));
}

static void test_a_fresh_challenge_drops_an_existing_session(void)
{
    TEST_ASSERT_EQUAL(AG_OK, challenge(0));
    ag_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(AG_OK, response(ARBITRARY_SIG));
    TEST_ASSERT_EQUAL(AG_OK, ag_auth_gate());

    TEST_ASSERT_EQUAL(AG_OK, challenge(0));   /* asking again re-arms and revokes */
    TEST_ASSERT_EQUAL(AG_ERR_AUTH_REQUIRED, ag_auth_gate());
}

/* --------------------------------------------------- no challenge outstanding */

static void test_a_response_with_no_challenge_outstanding_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, response(ARBITRARY_SIG));
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
    TEST_ASSERT_EQUAL(AG_OK, challenge(0));
    ag_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(AG_OK, response(ARBITRARY_SIG));

    TEST_ASSERT_EQUAL(AG_OK, eeprom_read(0u, 4u));
}

/* A read that STARTS above the boundary is never gated -- general-purpose
 * EEPROM, exactly as it always was. */
static void test_a_read_above_the_reserved_region_is_never_gated(void)
{
    TEST_ASSERT_EQUAL(AG_OK, eeprom_read(AG_BOOT_EE_LEN, 4u));
}

/* RESET(1) is accepted without a session. Mode 1 is not yet a distinct
 * bootloader entry (it takes the same reset as mode 0), so a gate would
 * protect nothing; the gate moves with issue #28, which makes it a real path. */
static void test_reset_mode_1_is_accepted_without_a_session(void)
{
    TEST_ASSERT_EQUAL(AG_OK, reset_bootloader());
    TEST_ASSERT_TRUE(ag_sys_reset_pending());
}

/* Having a session changes nothing about RESET(1) either, until #28. */
static void test_reset_mode_1_is_accepted_with_a_session(void)
{
    TEST_ASSERT_EQUAL(AG_OK, challenge(0));
    ag_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(AG_OK, response(ARBITRARY_SIG));

    TEST_ASSERT_EQUAL(AG_OK, reset_bootloader());
    TEST_ASSERT_TRUE(ag_sys_reset_pending());
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
    RUN_TEST(test_a_wrong_signature_is_refused_and_leaves_the_gate_closed);
    RUN_TEST(test_a_second_response_to_the_same_challenge_is_refused);
    RUN_TEST(test_a_fresh_challenge_drops_an_existing_session);
    RUN_TEST(test_a_response_with_no_challenge_outstanding_is_refused);
    RUN_TEST(test_a_short_response_is_refused);
    RUN_TEST(test_reserved_region_read_is_refused_before_authentication);
    RUN_TEST(test_reserved_region_read_succeeds_after_authentication);
    RUN_TEST(test_a_read_above_the_reserved_region_is_never_gated);
    RUN_TEST(test_reset_mode_1_is_accepted_without_a_session);
    RUN_TEST(test_reset_mode_1_is_accepted_with_a_session);
    RUN_TEST(test_reset_with_an_unknown_mode_is_refused);
    RUN_TEST(test_a_soft_reset_is_never_gated);
    return UNITY_END();
}
