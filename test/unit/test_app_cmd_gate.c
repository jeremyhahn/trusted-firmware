/*
 * test_app_cmd_gate.c -- the application's session view against a gate that
 * closes (#635).
 *
 * The host library links the permissive gate, so test_app_cmd only ever sees
 * tfw_app_session_ok() read 1. This test is its own gate, open or closed, and
 * pins both answers. The core does not gate APP_CMD: the application asks per
 * sub-op (docs/application-sdk/adr-application-wire-commands.md, owner ruling
 * 1243, option b), so a running application is still called with the gate
 * closed.
 */
#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "app_runner.h"
#include "auth_session.h"
#include "app_mock.h"
#include "hal_mock.h"
#include "tfw/app.h"
#include "tfw/protocol.h"

static uint8_t      session_open;
static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

/* The gate the core asks: this test's, in place of auth_session_none.c. */
void tfw_auth_session_init(void) { session_open = 0u; }

tfw_status_t tfw_auth_gate(void)
{
    return session_open ? TFW_OK : TFW_ERR_AUTH_REQUIRED;
}

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_mock_app_reset();
    tfw_app_runner_reset_state();
    session_open = 0u;
}

void tearDown(void) {}

static tfw_status_t app_cmd(const uint8_t *req, uint8_t len)
{
    memcpy(inbuf, req, len);
    tfw_reader_init(&r, inbuf, len);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(TFW_CMD_APP_CMD, &r, &w);
}

static void test_session_ok_is_0_with_the_gate_closed(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_app_session_ok());
}

static void test_session_ok_is_1_with_the_gate_open(void)
{
    session_open = 1u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_app_session_ok());
    session_open = 0u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_app_session_ok());
}

static void test_a_running_application_gets_app_cmd_with_the_gate_closed(void)
{
    static const uint8_t req[]   = { 0x2Au, 0x01u };
    static const uint8_t reply[] = { 0x5Au };

    tfw_mock_app_set_command(TFW_OK, reply, (uint8_t)sizeof reply);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_STRING("ic", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_HEX8(0x2Au, tfw_mock_app_command_op());
    TEST_ASSERT_EQUAL_UINT8(sizeof reply, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(reply, outbuf, sizeof reply);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_session_ok_is_0_with_the_gate_closed);
    RUN_TEST(test_session_ok_is_1_with_the_gate_open);
    RUN_TEST(test_a_running_application_gets_app_cmd_with_the_gate_closed);
    return UNITY_END();
}
