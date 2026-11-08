/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The auth session (src/core/auth_session.c) against the ATECC608A model, with
 * test_auth_session's link, handlers called directly as test_auth_session
 * calls them:
 *   - the AUTH_RESPONSE frame mutated at every byte: each of its eight bits,
 *     and the byte at 0x00 and 0xFF. Every value of every byte is 16 320 P-256
 *     verifies in the model, past the 10 s bound under the sanitizers; these
 *     cover each byte's every bit and both extremes;
 *   - every frame shorter than the signature;
 *   - a response replayed, before any challenge, to a superseded challenge,
 *     and to the next one;
 *   - the session's ends: a new challenge, a refused response after one, and
 *     a reset. auth_session.c keeps no clock, so these are its expiry.
 * A refusal is what test_auth_session defines: a status other than TFW_OK and
 * the gate closed. Exhaustive, so there is no seed; each failure names its
 * case.
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "auth_session.h"
#include "storage.h"
#include "sys.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "tfw/status.h"
#include "tfw/hal.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "boot_platform.h"
#include "platform_id.h"
#include "handoff.h"
#include "sha256.h"

/* test_auth_session's HAL stand-ins: the element model is the I2C HAL here. */
tfw_ms_t     tfw_hal_millis(void)              { return 0u; }
uint32_t    tfw_hal_free_ram(void)            { return 0u; }
tfw_reset_cause_t tfw_hal_reset_cause(void)    { return TFW_RESET_UNKNOWN; }
uint8_t     tfw_hal_uart_tx_idle(void)        { return 1u; }
void        tfw_hal_reset(void)               {}

static uint8_t test_eeprom[TFW_EEPROM_SIZE];
static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];

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

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    return tfw_hal_eeprom_read(addr, buf, len);
}

static const uint8_t SERIAL[TFW_BOOT_SERIAL_LEN] = {
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11
};
static uint8_t operator_priv[32];
static uint8_t operator_pub[TFW_BOOT_OPERATOR_PUBKEY_LEN];
static uint8_t pristine[TFW_EEPROM_SIZE];
static char    why[96];

static void fresh(void)
{
    tfw_model_reset();
    tfw_model_provision();
    memcpy(test_eeprom, pristine, sizeof test_eeprom);
    tfw_platform_id_init();
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_boot_handoff_init();
    tfw_crypto_init();
    tfw_auth_session_init();
    tfw_sys_reset_state();
}

void setUp(void)
{
    tfw_boot_platform_t plat;

    tfw_model_reset();
    tfw_model_p256_key("operator", operator_priv, operator_pub);
    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, SERIAL, sizeof SERIAL);
    memcpy(plat.operator_pubkey, operator_pub, sizeof operator_pub);
    memset(pristine, 0, sizeof pristine);
    tfw_boot_platform_serialise(&plat, pristine);
    fresh();
}
void tearDown(void) {}

static void sign(const uint8_t n[TFW_AUTH_NONCE_LEN], uint8_t sig[TFW_AUTH_SIG_LEN])
{
    static const char domain[] = "tfw-auth-v1";
    uint8_t           msg[sizeof domain - 1u + TFW_BOOT_SERIAL_LEN + TFW_AUTH_NONCE_LEN];
    uint8_t           digest[32];

    memcpy(msg, domain, sizeof domain - 1u);
    memcpy(msg + sizeof domain - 1u, SERIAL, TFW_BOOT_SERIAL_LEN);
    memcpy(msg + sizeof domain - 1u + TFW_BOOT_SERIAL_LEN, n, TFW_AUTH_NONCE_LEN);
    tfw_sha256(msg, (uint16_t)sizeof msg, digest);
    TEST_ASSERT_TRUE(tfw_model_p256_sign(operator_priv, digest, sig));
}

static void challenge(uint8_t n[TFW_AUTH_NONCE_LEN])
{
    uint8_t      out[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, NULL, 0u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_auth_cmd_challenge(&r, &w), why);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_AUTH_NONCE_LEN, tfw_writer_len(&w), why);
    memcpy(n, out, TFW_AUTH_NONCE_LEN);
}

static tfw_status_t respond(const uint8_t *sig, uint8_t n)
{
    uint8_t      out[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, sig, n);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    return tfw_auth_cmd_response(&r, &w);
}

static void expect_refused(const uint8_t *sig, uint8_t n)
{
    TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, respond(sig, n), why);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate(), why);
}

/* A genuine round: the gate opens. */
static void open_session(uint8_t sig[TFW_AUTH_SIG_LEN])
{
    uint8_t n[TFW_AUTH_NONCE_LEN];

    challenge(n);
    sign(n, sig);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, respond(sig, TFW_AUTH_SIG_LEN), why);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_auth_gate(), why);
}

static void test_every_response_byte_mutated_is_refused(void)
{
    static const uint8_t extremes[] = { 0x00u, 0xFFu };
    uint8_t  n[TFW_AUTH_NONCE_LEN];
    uint8_t  good[TFW_AUTH_SIG_LEN];
    uint8_t  sig[TFW_AUTH_SIG_LEN];
    unsigned i;
    unsigned k;
    unsigned v;

    for (i = 0u; i < TFW_AUTH_SIG_LEN; i++) {
        for (k = 0u; k < 8u + sizeof extremes; k++) {
            challenge(n);
            sign(n, good);
            memcpy(sig, good, sizeof sig);
            v = k < 8u ? (unsigned)(good[i] ^ (1u << k)) : extremes[k - 8u];
            if (v == good[i]) {
                (void)respond(good, TFW_AUTH_SIG_LEN);   /* spend the challenge */
                continue;
            }
            sig[i] = (uint8_t)v;
            snprintf(why, sizeof why, "response byte %u = 0x%02X", i, v);
            expect_refused(sig, TFW_AUTH_SIG_LEN);
            /* spent: the genuine one after it is refused too */
            expect_refused(good, TFW_AUTH_SIG_LEN);
        }
    }
}

static void test_every_short_frame_is_refused_and_keeps_the_challenge(void)
{
    uint8_t  n[TFW_AUTH_NONCE_LEN];
    uint8_t  sig[TFW_AUTH_SIG_LEN];
    unsigned len;

    for (len = 0u; len < TFW_AUTH_SIG_LEN; len++) {
        snprintf(why, sizeof why, "a %u-byte response", len);
        challenge(n);
        sign(n, sig);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, respond(sig, (uint8_t)len), why);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate(), why);
        /* The dispatcher refuses it before here on the wire; the handler's own
         * refusal leaves the challenge for the whole frame. */
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, respond(sig, TFW_AUTH_SIG_LEN), why);
        tfw_auth_session_init();
    }
}

static void test_replayed_and_reordered_responses_are_refused(void)
{
    uint8_t n1[TFW_AUTH_NONCE_LEN];
    uint8_t n2[TFW_AUTH_NONCE_LEN];
    uint8_t s1[TFW_AUTH_SIG_LEN];
    uint8_t s2[TFW_AUTH_SIG_LEN];

    /* before any challenge, with a signature over the nonce one will draw */
    snprintf(why, sizeof why, "response before a challenge");
    challenge(n1);
    sign(n1, s1);
    fresh();
    expect_refused(s1, TFW_AUTH_SIG_LEN);

    /* to a superseded challenge: the newer one stands, the older is gone */
    snprintf(why, sizeof why, "response to a superseded challenge");
    challenge(n1);
    sign(n1, s1);
    challenge(n2);
    TEST_ASSERT_TRUE_MESSAGE(memcmp(n1, n2, sizeof n1) != 0, why);
    sign(n2, s2);
    expect_refused(s1, TFW_AUTH_SIG_LEN);
    expect_refused(s2, TFW_AUTH_SIG_LEN);   /* the refusal spent it */

    /* replayed against the challenge it answered: once only */
    snprintf(why, sizeof why, "response replayed");
    fresh();
    open_session(s1);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_CRYPTO, respond(s1, TFW_AUTH_SIG_LEN), why);
    /* and against the next challenge: refused, the session closed */
    snprintf(why, sizeof why, "response replayed to the next challenge");
    challenge(n2);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate(), why);
    expect_refused(s1, TFW_AUTH_SIG_LEN);
}

static void test_a_session_ends_and_stays_ended(void)
{
    uint8_t n[TFW_AUTH_NONCE_LEN];
    uint8_t sig[TFW_AUTH_SIG_LEN];
    uint8_t bad[TFW_AUTH_SIG_LEN];

    snprintf(why, sizeof why, "ended by a new challenge");
    open_session(sig);
    challenge(n);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate(), why);

    snprintf(why, sizeof why, "a refused response after the new challenge");
    sign(n, bad);
    bad[TFW_AUTH_SIG_LEN - 1u] ^= 0x01u;
    expect_refused(bad, TFW_AUTH_SIG_LEN);
    expect_refused(sig, TFW_AUTH_SIG_LEN);

    snprintf(why, sizeof why, "ended by a reset");
    open_session(sig);
    tfw_auth_session_init();
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate(), why);
    expect_refused(sig, TFW_AUTH_SIG_LEN);

    snprintf(why, sizeof why, "a fresh round after the ends");
    open_session(sig);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_response_byte_mutated_is_refused);
    RUN_TEST(test_every_short_frame_is_refused_and_keeps_the_challenge);
    RUN_TEST(test_replayed_and_reordered_responses_are_refused);
    RUN_TEST(test_a_session_ends_and_stays_ended);
    return UNITY_END();
}
