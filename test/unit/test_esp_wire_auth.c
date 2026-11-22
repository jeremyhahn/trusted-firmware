/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The operator session on the ESP32 wire (#607, Codex round 2): the P4 links
 * src/core/auth_session.c behind src/hal/esp32/hal_uart.c's TCP byte link, so
 * a grant must belong to the connection that earned it. A host that replaces
 * a quiet one, or dials in after one hung up, starts with the gate closed and
 * cannot answer a challenge the host before it left pending; it authenticates
 * on its own, and then it is let in.
 *
 * Built like test_esp_wire (mock lwIP, real transport and dispatch) with
 * auth_session.c in place of auth_session_none.c. The soft provider refuses
 * both a nonce and an operator verify, so this suite stands in for the two:
 * nonces from a counter, and an operator "signature" that is the digest twice.
 * The digest still binds domain, serial and nonce, so an answer to another
 * challenge does not pass.
 */
#include <errno.h>
#include <string.h>
#include "unity.h"
#include "transport.h"
#include "frame.h"
#include "sys.h"
#include "hal_mock.h"
#include "ota_stage.h"
#include "esp_ota_mock.h"
#include "lwip_mock.h"
#include "hal_esp32.h"
#include "auth_session.h"
#include "platform_id.h"
#include "sha256.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"

#define NO_REPLY (-1)

static tfw_transport_t t;
static uint8_t         seq;
static uint8_t         nonce_ctr;
static uint8_t         got[TFW_MAX_PAYLOAD];
static uint8_t         got_len;

/* The Ethernet link under the socket is hal_eth.c's, and not this suite's. */
tfw_status_t tfw_esp_eth_start(void)
{
    return TFW_OK;
}

/* A fresh nonce per challenge: the element's TRNG on the part. */
tfw_status_t tfw_crypto_random(uint8_t *out, uint8_t len)
{
    nonce_ctr++;
    memset(out, nonce_ctr, len);
    return TFW_OK;
}

/* The operator's verify: the signature is the digest, twice. */
tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[32], const uint8_t sig[64],
                                        const uint8_t pubkey[64])
{
    (void)pubkey;
    if (memcmp(sig, digest, 32u) != 0 || memcmp(&sig[32], digest, 32u) != 0) {
        return TFW_ERR_CRYPTO;
    }
    return TFW_OK;
}

void setUp(void)
{
    unsigned i, n;

    /* hal_uart.c keeps its host from one test to the next, as the device does:
     * hang up whatever the last test left and let the wire let it go. */
    for (i = 0u; i < mock_lwip.nconn; i++) {
        mock_lwip.conn[i].in_pos     = mock_lwip.conn[i].in_len;
        mock_lwip.conn[i].hung_up    = 1u;
        mock_lwip.conn[i].recv_errno = 0;
        mock_lwip.conn[i].send_errno = 0;
        mock_lwip.conn[i].pending    = 0u;
    }
    for (n = 0u; n < 4u; n++) {
        uint8_t b;
        (void)tfw_hal_uart_rx(&b);
    }
    tfw_stage_init();
    mock_lwip_reset();
    tfw_mock_reset();
    tfw_sys_reset_state();
    mock_esp_reset();
    tfw_stage_init();
    tfw_transport_init(&t);
    tfw_auth_session_init();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
}

void tearDown(void) {}

/* Polls until c has a reply to cmd; its status, its payload in got. */
static int reply_on(mock_conn_t *c, uint8_t cmd)
{
    tfw_framer_t f;
    unsigned     i, j;

    for (i = 0u; i < 64u; i++) {
        tfw_transport_poll(&t);
        tfw_framer_init(&f);
        for (j = 0u; j < c->out_len; j++) {
            if (tfw_framer_push(&f, c->out[j]) == TFW_FRAME_READY) {
                c->out_len = 0u;
                TEST_ASSERT_EQUAL_HEX8(cmd | TFW_OP_RESPONSE_BIT, tfw_frame_op(&f));
                TEST_ASSERT_EQUAL_UINT8(seq, tfw_frame_seq(&f));
                got_len = tfw_frame_payload_len(&f);
                memcpy(got, tfw_frame_payload(&f), got_len);
                return (int)tfw_frame_status(&f);
            }
        }
    }
    return NO_REPLY;
}

static int ask(mock_conn_t *c, uint8_t cmd, const uint8_t *pay, uint8_t n)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len;

    seq++;
    len = tfw_frame_encode_request(wire, sizeof wire, seq, cmd, pay, n);
    TEST_ASSERT_NOT_EQUAL(0u, len);
    mock_lwip_feed(c, wire, len);
    return reply_on(c, cmd);
}

static mock_conn_t *host(void)
{
    mock_conn_t *c = mock_lwip_connect();

    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(c, TFW_CMD_PING, NULL, 0u));
    TEST_ASSERT_TRUE(c->open);
    return c;
}

/* AUTH_CHALLENGE on c; the nonce it answers with. */
static void challenge(mock_conn_t *c, uint8_t nonce[TFW_AUTH_NONCE_LEN])
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(c, TFW_CMD_AUTH_CHALLENGE, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT8(TFW_AUTH_NONCE_LEN, got_len);
    memcpy(nonce, got, TFW_AUTH_NONCE_LEN);
}

/* AUTH_RESPONSE on c, the operator's answer to nonce. */
static int respond(mock_conn_t *c, const uint8_t nonce[TFW_AUTH_NONCE_LEN])
{
    static const char domain[] = "tfw-auth-v1";
    uint8_t           msg[sizeof domain - 1u + TFW_BOOT_SERIAL_LEN + TFW_AUTH_NONCE_LEN];
    uint8_t           sig[TFW_AUTH_SIG_LEN];

    memcpy(msg, domain, sizeof domain - 1u);
    memcpy(&msg[sizeof domain - 1u], tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN);
    memcpy(&msg[sizeof domain - 1u + TFW_BOOT_SERIAL_LEN], nonce, TFW_AUTH_NONCE_LEN);
    tfw_sha256(msg, (uint16_t)sizeof msg, sig);
    memcpy(&sig[32], sig, 32u);
    return ask(c, TFW_CMD_AUTH_RESPONSE, sig, (uint8_t)sizeof sig);
}

static void authenticate(mock_conn_t *c)
{
    uint8_t nonce[TFW_AUTH_NONCE_LEN];

    challenge(c, nonce);
    TEST_ASSERT_EQUAL_INT(TFW_OK, respond(c, nonce));
}

/* A gated command: a read of the boot-owned EEPROM region. */
static int gated(mock_conn_t *c)
{
    static const uint8_t pay[3] = { 0u, 0u, 4u };

    return ask(c, TFW_CMD_EEPROM_READ, pay, (uint8_t)sizeof pay);
}

static void test_the_gate_opens_for_the_host_that_authenticated(void)
{
    mock_conn_t *a = host();

    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, gated(a));
    authenticate(a);
    TEST_ASSERT_EQUAL_INT(TFW_OK, gated(a));
}

static void test_a_host_that_replaces_a_quiet_one_must_authenticate(void)
{
    mock_conn_t *a = host(), *b;

    authenticate(a);
    TEST_ASSERT_EQUAL_INT(TFW_OK, gated(a));
    tfw_mock_advance(TFW_ESP_WIRE_IDLE_MS);
    b = host();
    TEST_ASSERT_TRUE(a->closed);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, gated(b));
    authenticate(b);                               /* on its own, it gets in */
    TEST_ASSERT_EQUAL_INT(TFW_OK, gated(b));
}

static void test_a_host_that_dials_in_after_a_hangup_must_authenticate(void)
{
    mock_conn_t *a = host(), *b;

    authenticate(a);
    a->hung_up = 1u;
    TEST_ASSERT_EQUAL_INT(NO_REPLY, reply_on(a, TFW_CMD_PING));
    TEST_ASSERT_TRUE(a->closed);
    b = host();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, gated(b));
}

static void test_a_challenge_left_pending_cannot_be_answered_by_the_next_host(void)
{
    mock_conn_t *a = host(), *b;
    uint8_t      nonce[TFW_AUTH_NONCE_LEN];

    challenge(a, nonce);
    tfw_mock_advance(TFW_ESP_WIRE_IDLE_MS);
    b = host();
    TEST_ASSERT_TRUE(a->closed);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, respond(b, nonce));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, gated(b));
}

/* OTA_ACTIVATE is gated on the operator session as well as on OTA_AUTH: OTA_AUTH
 * is not gated, so a host that never authenticated can hold a signature. Until
 * it authenticates, the activate is refused at the gate and writes nothing; after,
 * it reaches the stage, which refuses on state (no image ended here). */
static void test_an_activate_from_a_host_not_authenticated_is_refused_at_the_gate(void)
{
    static const uint8_t mode = 0u;
    uint8_t              sig[TFW_OTA_AUTH_SIG_LEN];
    mock_conn_t         *a = host();

    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(a, TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, ask(a, TFW_CMD_OTA_ACTIVATE, &mode, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.set_boots);

    authenticate(a);
    TEST_ASSERT_EQUAL_INT(TFW_OK, ask(a, TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, ask(a, TFW_CMD_OTA_ACTIVATE, &mode, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_gate_opens_for_the_host_that_authenticated);
    RUN_TEST(test_a_host_that_replaces_a_quiet_one_must_authenticate);
    RUN_TEST(test_a_host_that_dials_in_after_a_hangup_must_authenticate);
    RUN_TEST(test_a_challenge_left_pending_cannot_be_answered_by_the_next_host);
    RUN_TEST(test_an_activate_from_a_host_not_authenticated_is_refused_at_the_gate);
    return UNITY_END();
}
