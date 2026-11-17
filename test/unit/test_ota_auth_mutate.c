/*
 * OTA_AUTH's hold (src/core/ota_auth.c, #163): the signature an OTA_AUTH
 * leaves for the OTA_ACTIVATE or OTA_CONFIRM right after it, alone, with the
 * dispatcher's record of the last command supplied by the test.
 *
 * The oracle is the hold's rule: tfw_ota_auth_take hands back the 64 bytes of
 * the last OTA_AUTH only when that OTA_AUTH was the last command dispatched,
 * it finished TFW_OK, and nothing has taken the hold since. Any other length
 * is refused and drops an earlier hold; a take always drops it.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/protocol.h"
#include "reader.h"
#include "writer.h"
#include "ota_auth.h"
#include "ota_ctl.h"
#include "ota_stage.h"
#include "auth_session.h"
#include "boot_platform.h"
#include "tfw/hal.h"
#include "../caps/sys.h"

/* OTA_ACTIVATE's seams (src/core/ota_activate.c), for row 28's case: the
 * element tier's operator session is tfw_auth_gate, open or closed by the
 * test; the store's state is an ended image, never a pending one. */
static uint8_t session_open;
static uint8_t marked;
static uint8_t authorized_with[TFW_OTA_AUTH_SIG_LEN];
/* The real tfw_stage_authorize erases the MARK sector before it writes, so a
 * refused activate must not reach it at all. */
static unsigned authorize_calls;

tfw_status_t tfw_auth_gate(void)
{
    return session_open ? TFW_OK : TFW_ERR_AUTH_REQUIRED;
}

uint8_t tfw_stage_install_pending(void)
{
    return 0u;
}

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    (void)addr;
    memset(buf, 0, len);
    return TFW_OK;
}

tfw_status_t tfw_boot_platform_parse(const uint8_t *raw, tfw_boot_platform_t *p)
{
    (void)raw;
    memset(p, 0, sizeof *p);
    p->boot_state = TFW_BOOT_STATE_GOOD;
    return TFW_OK;
}

tfw_status_t tfw_ota_manifest(tfw_boot_platform_t *p)
{
    (void)p;
    return TFW_OK;
}

tfw_status_t tfw_stage_authorize(const uint8_t sig[TFW_OTA_AUTH_SIG_LEN], uint8_t mode)
{
    (void)mode;
    authorize_calls++;
    if (sig == NULL) {
        return TFW_ERR_AUTH_REQUIRED;
    }
    memcpy(authorized_with, sig, TFW_OTA_AUTH_SIG_LEN);
    return TFW_OK;
}

tfw_status_t tfw_stage_activate(void)
{
    marked = 1u;
    return TFW_OK;
}

void tfw_sys_reset_soon(void) {}

/* The dispatcher's record, as dispatch.c keeps it for the last command. */
static uint8_t last_cmd;
static uint8_t last_ok;

uint8_t tfw_dispatch_last(void)
{
    return last_cmd;
}

uint8_t tfw_dispatch_last_ok(void)
{
    return last_ok;
}

static uint8_t req[TFW_OTA_AUTH_SIG_LEN + 2u];
static uint8_t out[8];

static void fill(uint8_t seed)
{
    for (uint16_t i = 0; i < sizeof req; i++) {
        req[i] = (uint8_t)(seed + i * 7u);
    }
}

/* One OTA_AUTH of len bytes, recorded as the dispatcher would record it. */
static tfw_status_t auth(uint8_t len)
{
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_reader_init(&r, req, len);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    tfw_status_t st = tfw_ota_auth_cmd(&r, &w);
    last_cmd = TFW_CMD_OTA_AUTH;
    last_ok = (uint8_t)(st == TFW_OK);
    return st;
}

void setUp(void)
{
    fill(0x11u);
    (void)auth(TFW_OTA_AUTH_SIG_LEN);
    last_ok = 0u;
    (void)tfw_ota_auth_take();
    last_cmd = 0u;
    last_ok = 0u;
}

void tearDown(void) {}

static void test_a_whole_signature_is_held_byte_for_byte(void)
{
    fill(0x5au);
    TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
    uint8_t want[TFW_OTA_AUTH_SIG_LEN];
    memcpy(want, req, sizeof want);
    memset(req, 0, sizeof req); /* the hold is a copy, not the frame */
    const uint8_t *sig = tfw_ota_auth_take();
    TEST_ASSERT_NOT_NULL(sig);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, sig, TFW_OTA_AUTH_SIG_LEN);
}

static void test_every_other_length_is_refused_and_holds_nothing(void)
{
    for (uint8_t len = 0; len <= TFW_OTA_AUTH_SIG_LEN + 2u; len++) {
        if (len == TFW_OTA_AUTH_SIG_LEN) {
            continue;
        }
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, auth(len));
        last_ok = 1u; /* even a record that says OK takes nothing */
        TEST_ASSERT_NULL(tfw_ota_auth_take());
    }
}

static void test_a_refused_length_drops_an_earlier_hold(void)
{
    for (uint8_t len = 0; len <= TFW_OTA_AUTH_SIG_LEN + 2u; len++) {
        if (len == TFW_OTA_AUTH_SIG_LEN) {
            continue;
        }
        TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, auth(len));
        last_ok = 1u;
        TEST_ASSERT_NULL(tfw_ota_auth_take());
    }
}

static void test_a_take_drops_the_hold(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
    TEST_ASSERT_NOT_NULL(tfw_ota_auth_take());
    TEST_ASSERT_NULL(tfw_ota_auth_take());
}

static void test_another_last_command_takes_nothing_and_drops_the_hold(void)
{
    for (uint16_t cmd = 0; cmd <= 0xffu; cmd++) {
        if (cmd == TFW_CMD_OTA_AUTH) {
            continue;
        }
        TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
        last_cmd = (uint8_t)cmd;
        TEST_ASSERT_NULL(tfw_ota_auth_take());
        last_cmd = TFW_CMD_OTA_AUTH;
        TEST_ASSERT_NULL(tfw_ota_auth_take());
    }
}

static void test_a_refused_last_dispatch_takes_nothing_and_drops_the_hold(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
    last_ok = 0u;
    TEST_ASSERT_NULL(tfw_ota_auth_take());
    last_ok = 1u;
    TEST_ASSERT_NULL(tfw_ota_auth_take());
}

static void test_no_auth_since_boot_takes_nothing(void)
{
    last_cmd = TFW_CMD_OTA_AUTH;
    last_ok = 1u;
    TEST_ASSERT_NULL(tfw_ota_auth_take());
}

static void test_a_second_auth_replaces_the_first(void)
{
    fill(0x01u);
    TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
    fill(0x80u);
    uint8_t want[TFW_OTA_AUTH_SIG_LEN];
    memcpy(want, req, sizeof want);
    TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
    const uint8_t *sig = tfw_ota_auth_take();
    TEST_ASSERT_NOT_NULL(sig);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, sig, TFW_OTA_AUTH_SIG_LEN);
}

static void test_the_reply_is_empty(void)
{
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_reader_init(&r, req, TFW_OTA_AUTH_SIG_LEN);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_ota_auth_cmd(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_reader_remaining(&r));
}

/* OTA_ACTIVATE, mode 0, as the dispatcher records it. */
static tfw_status_t activate(void)
{
    uint8_t mode = 0u;
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_reader_init(&r, &mode, 1u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    tfw_status_t st = tfw_otactl_cmd_activate(&r, &w);
    last_cmd = TFW_CMD_OTA_ACTIVATE;
    last_ok = (uint8_t)(st == TFW_OK);
    return st;
}

/* Audit row 28 (docs/ota/engineering.md): the element tier keeps its operator
 * session and OTA_AUTH is checked as well. Neither alone activates. */
static void test_the_element_session_and_ota_auth_are_each_needed_to_activate(void)
{
    marked = 0u;
    authorize_calls = 0u;
    /* A session without OTA_AUTH: refused before the stage is touched. */
    session_open = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate());
    TEST_ASSERT_EQUAL_UINT8(0u, marked);
    TEST_ASSERT_EQUAL_UINT(0u, authorize_calls);
    /* OTA_AUTH without a session. */
    session_open = 0u;
    fill(0x33u);
    TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate());
    TEST_ASSERT_EQUAL_UINT8(0u, marked);
    TEST_ASSERT_EQUAL_UINT(0u, authorize_calls);
    /* Both: the held signature is the one authorized. */
    session_open = 1u;
    fill(0x44u);
    uint8_t want[TFW_OTA_AUTH_SIG_LEN];
    memcpy(want, req, sizeof want);
    TEST_ASSERT_EQUAL(TFW_OK, auth(TFW_OTA_AUTH_SIG_LEN));
    TEST_ASSERT_EQUAL(TFW_OK, activate());
    TEST_ASSERT_EQUAL_UINT8(1u, marked);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, authorized_with, TFW_OTA_AUTH_SIG_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_whole_signature_is_held_byte_for_byte);
    RUN_TEST(test_every_other_length_is_refused_and_holds_nothing);
    RUN_TEST(test_a_refused_length_drops_an_earlier_hold);
    RUN_TEST(test_a_take_drops_the_hold);
    RUN_TEST(test_another_last_command_takes_nothing_and_drops_the_hold);
    RUN_TEST(test_a_refused_last_dispatch_takes_nothing_and_drops_the_hold);
    RUN_TEST(test_no_auth_since_boot_takes_nothing);
    RUN_TEST(test_a_second_auth_replaces_the_first);
    RUN_TEST(test_the_reply_is_empty);
    RUN_TEST(test_the_element_session_and_ota_auth_are_each_needed_to_activate);
    return UNITY_END();
}
