/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * APP_CMD against the stand-in tfw_app_command (src/core/app_cmd_none.c), the
 * one an application links when its app.mk does not set APP_COMMANDS := yes.
 * The application here is the least one that runs: a descriptor and the
 * lifecycle hooks, and no command hook of its own. Linked with the real runner
 * and without the recording mock (mk/host.mk).
 */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "app_runner.h"
#include "hal_mock.h"
#include "tfw/app.h"
#include "tfw/protocol.h"

const tfw_app_descriptor_t tfw_app_descriptor TFW_PROGMEM = {
    .magic = TFW_APP_MAGIC, .abi = TFW_APP_ABI, .ota_slots = TFW_APP_OTA_NONE,
    .version_major = 1u, .name = "nocmd",
};

tfw_status_t tfw_app_init(void)         { return TFW_OK; }
void tfw_app_run(tfw_ms_t now_ms)       { (void)now_ms; }
void tfw_app_shutdown(void)             {}
void tfw_app_before_update(void)        {}
tfw_status_t tfw_app_after_update(void) { return TFW_OK; }

tfw_status_t tfw_app_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_app_runner_reset_state();
    tfw_app_runner_start();
}

void tearDown(void) {}

static tfw_status_t app_cmd(const uint8_t *req, uint8_t len)
{
    memcpy(inbuf, req, len);
    tfw_reader_init(&r, inbuf, len);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(TFW_CMD_APP_CMD, &r, &w);
}

static void assert_unsupported_and_empty(const uint8_t *req, uint8_t len)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, app_cmd(req, len));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_the_application_runs(void)
{
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

static void test_op_00_is_unsupported(void)
{
    static const uint8_t req[] = { 0x00u };
    assert_unsupported_and_empty(req, (uint8_t)sizeof req);
}

static void test_op_01_with_an_argument_is_unsupported(void)
{
    static const uint8_t req[] = { 0x01u, 0x55u };
    assert_unsupported_and_empty(req, (uint8_t)sizeof req);
}

static void test_op_ff_is_unsupported(void)
{
    static const uint8_t req[] = { 0xFFu };
    assert_unsupported_and_empty(req, (uint8_t)sizeof req);
}

static void test_an_empty_request_is_bad_argc(void)
{
    static const uint8_t req[] = { 0x00u };
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, app_cmd(req, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_a_stopped_application_is_bad_state(void)
{
    static const uint8_t req[] = { 0x00u };
    tfw_app_runner_stop(0u);
    TEST_ASSERT_NOT_EQUAL(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_application_runs);
    RUN_TEST(test_op_00_is_unsupported);
    RUN_TEST(test_op_01_with_an_argument_is_unsupported);
    RUN_TEST(test_op_ff_is_unsupported);
    RUN_TEST(test_an_empty_request_is_bad_argc);
    RUN_TEST(test_a_stopped_application_is_bad_state);
    return UNITY_END();
}
