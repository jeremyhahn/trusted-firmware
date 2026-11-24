/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * APP_CMD (0x07) through the dispatcher, against the recording application
 * (test/mock/app_mock.c): the handler in src/core/app_runner.c reads the
 * application's sub-op, calls tfw_app_command only while the application
 * runs, and times the call against the run budget. The ADR is
 * docs/application-sdk/adr-application-wire-commands.md (decisions 1-4).
 */

#include <stdio.h>
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
#include "tfw/config.h"

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_mock_app_reset();
    tfw_app_runner_reset_state();
}

void tearDown(void) {}

/* APP_CMD with the given payload, into a reply buffer of cap bytes. */
static tfw_status_t app_cmd_cap(const uint8_t *req, uint8_t len, uint8_t cap)
{
    if (len != 0u) {
        memcpy(inbuf, req, len);
    }
    tfw_reader_init(&r, inbuf, len);
    tfw_writer_init(&w, outbuf, cap);
    return tfw_dispatch(TFW_CMD_APP_CMD, &r, &w);
}

static tfw_status_t app_cmd(const uint8_t *req, uint8_t len)
{
    return app_cmd_cap(req, len, (uint8_t)sizeof outbuf);
}

/* ---------------------------------------------------- the dispatcher's bounds */

/* No app_op is BAD_ARGC from the dispatcher, before the handler runs. */
static void test_an_empty_request_is_bad_argc_and_calls_nothing(void)
{
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, app_cmd(NULL, 0u));
    TEST_ASSERT_EQUAL_STRING("i", tfw_mock_app_log());
}

/* One op byte and TFW_APP_CMD_MAX_ARGS of arguments is the whole payload. */
static void test_the_largest_request_is_admitted(void)
{
    uint8_t req[1u + TFW_APP_CMD_MAX_ARGS];

    TEST_ASSERT_EQUAL_UINT(TFW_MAX_PAYLOAD, sizeof req);
    memset(req, 0xA5, sizeof req);
    req[0] = 0x10u;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_CMD_MAX_ARGS, tfw_mock_app_command_args_len());
}

/* ------------------------------------------------ only while it runs (dec. 4) */

static void assert_bad_state_and_uncalled(const char *log)
{
    static const uint8_t req[] = { 0x00u };

    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_STRING(log, tfw_mock_app_log());
}

static void test_an_application_not_started_is_bad_state(void)
{
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_IDLE, tfw_app_runner_state());
    assert_bad_state_and_uncalled("");
}

static void test_an_application_whose_init_failed_is_bad_state(void)
{
    tfw_mock_app_set_init(TFW_ERR_TIMEOUT);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_FAILED, tfw_app_runner_state());
    assert_bad_state_and_uncalled("i");
}

static void test_a_refused_descriptor_is_bad_state(void)
{
    tfw_mock_app_descriptor()->magic = 0u;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
    assert_bad_state_and_uncalled("");
}

static void test_a_stopped_application_is_bad_state(void)
{
    tfw_app_runner_start();
    tfw_app_runner_stop(0u);
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_STOPPED, tfw_app_runner_state());
    assert_bad_state_and_uncalled("is");
}

/* ----------------------------------------------------- the hook's own answer */

/* RUNNING: the op and the arguments reach the hook, and its bytes are the reply. */
static void test_a_running_application_gets_the_op_and_args_and_answers(void)
{
    static const uint8_t req[]   = { 0x2Au, 0x01u, 0x02u, 0x03u };
    static const uint8_t reply[] = { 0xDEu, 0xADu };
    uint8_t n = 0u;

    tfw_mock_app_set_command(TFW_OK, reply, (uint8_t)sizeof reply);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_STRING("ic", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_HEX8(0x2Au, tfw_mock_app_command_op());
    n = tfw_mock_app_command_args_len();
    TEST_ASSERT_EQUAL_UINT8(3u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&req[1], tfw_mock_app_command_args(), n);
    TEST_ASSERT_EQUAL_UINT8(sizeof reply, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(reply, outbuf, sizeof reply);
}

/* The hook's refusal is the answer, whatever it wrote. */
static void test_the_hooks_refusal_is_the_answer(void)
{
    static const uint8_t req[] = { 0x01u };

    tfw_mock_app_set_command(TFW_ERR_UNSUPPORTED, NULL, 0u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_STRING("ic", tfw_mock_app_log());
}

/* --------------------------------------- the dispatcher's post-checks (dec. 3) */

/* A hook that reads past its arguments has misparsed them: BAD_ARGC. */
static void test_a_hook_reading_past_its_args_is_bad_argc(void)
{
    static const uint8_t req[] = { 0x05u, 0x11u };

    tfw_mock_app_set_command_reads(2u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_STRING("ic", tfw_mock_app_log());
}

/* A reply that does not fit is TOO_LONG. */
static void test_a_reply_that_overflows_is_too_long(void)
{
    static const uint8_t req[]   = { 0x05u };
    static const uint8_t reply[] = { 1u, 2u, 3u, 4u };

    tfw_mock_app_set_command(TFW_OK, reply, (uint8_t)sizeof reply);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TOO_LONG, app_cmd_cap(req, (uint8_t)sizeof req, 3u));
}

/* ---------------------------------------------------------- the run budget */

static uint16_t app_info_overruns(void)
{
    tfw_reader_init(&r, inbuf, 0u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_dispatch(TFW_CMD_APP_INFO, &r, &w));
    return (uint16_t)(outbuf[1] | (uint16_t)(outbuf[2] << 8));
}

static void test_a_call_over_the_budget_counts_one_overrun(void)
{
    static const uint8_t req[] = { 0x00u };

    tfw_mock_app_set_command_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_UINT16(1u, tfw_app_runner_overruns());
    TEST_ASSERT_EQUAL_UINT16(1u, app_info_overruns());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

static void test_a_call_at_the_budget_counts_none(void)
{
    static const uint8_t req[] = { 0x00u };

    tfw_mock_app_set_command_ms(TFW_APP_RUN_BUDGET_MS);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_UINT16(0u, app_info_overruns());
}

/* A refused call is not a call: not timed, not counted. */
static void test_a_refused_call_counts_no_overrun(void)
{
    static const uint8_t req[] = { 0x00u };

    tfw_mock_app_set_command_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    tfw_app_runner_stop(0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_app_runner_overruns());
}

/* The APP_CMD path holds the count at its ceiling, as the poll path does: a
 * count that wrapped would read as no overruns at all. */
static void test_overruns_through_app_cmd_stop_at_0xffff(void)
{
    static const uint8_t req[] = { 0x00u };
    uint32_t             i;

    tfw_mock_app_set_command_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    for (i = 0u; i < 0xFFFFu; i++) {
        TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    }
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, tfw_app_runner_overruns());
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, tfw_app_runner_overruns());
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, app_info_overruns());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

/* A call that starts just before the millisecond-counter wrap is timed by
 * difference, as a run is: each start k ms before the wrap, each call length. */
static void test_a_call_across_the_millisecond_wrap_is_timed_by_difference(void)
{
    static const uint8_t req[] = { 0x00u };
    tfw_ms_t             k;
    tfw_ms_t             run;

    for (k = 0u; k <= 2u * TFW_APP_RUN_BUDGET_MS + 2u; k++) {
        for (run = 0u; run <= 2u * TFW_APP_RUN_BUDGET_MS + 1u; run++) {
            uint16_t     want = (run > TFW_APP_RUN_BUDGET_MS) ? 1u : 0u;
            tfw_status_t st;

            tfw_mock_reset();
            tfw_mock_app_reset();
            tfw_app_runner_reset_state();
            tfw_mock_set_millis((tfw_ms_t)(0u - k));
            tfw_mock_app_set_command_ms(run);
            tfw_app_runner_start();
            st = app_cmd(req, (uint8_t)sizeof req);
            if (st != TFW_OK || tfw_app_runner_state() != TFW_APP_STATE_RUNNING ||
                tfw_app_runner_overruns() != want) {
                printf("start %lu ms before the wrap, call %lu ms: status %d, state %d, overruns %u, want %u\n",
                       (unsigned long)k, (unsigned long)run, (int)st, (int)tfw_app_runner_state(),
                       (unsigned)tfw_app_runner_overruns(), (unsigned)want);
                TEST_FAIL_MESSAGE("an APP_CMD call across the millisecond-counter wrap is timed wrongly");
            }
        }
    }
}

/* The budget holds whatever the hook answers: a slow call it refuses still
 * overran. */
static void test_a_slow_call_the_hook_refuses_counts_one_overrun(void)
{
    static const uint8_t req[] = { 0x01u };

    tfw_mock_app_set_command(TFW_ERR_UNSUPPORTED, NULL, 0u);
    tfw_mock_app_set_command_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_STRING("ic", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_UINT16(1u, tfw_app_runner_overruns());
    TEST_ASSERT_EQUAL_UINT16(1u, app_info_overruns());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

/* APP_CMD and run count into the one overrun count APP_INFO reports. */
static void test_app_cmd_and_run_share_the_overrun_count(void)
{
    static const uint8_t req[] = { 0x00u };

    tfw_mock_app_set_command_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_mock_app_set_run_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT16(2u, app_info_overruns());
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(req, (uint8_t)sizeof req));
    TEST_ASSERT_EQUAL_UINT16(3u, app_info_overruns());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

/* ---------------------------------------------- the session view (dec. 5) */

/* The host library links the permissive gate, so the view says open. */
static void test_session_ok_follows_the_auth_gate(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_auth_gate());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_app_session_ok());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_empty_request_is_bad_argc_and_calls_nothing);
    RUN_TEST(test_the_largest_request_is_admitted);
    RUN_TEST(test_an_application_not_started_is_bad_state);
    RUN_TEST(test_an_application_whose_init_failed_is_bad_state);
    RUN_TEST(test_a_refused_descriptor_is_bad_state);
    RUN_TEST(test_a_stopped_application_is_bad_state);
    RUN_TEST(test_a_running_application_gets_the_op_and_args_and_answers);
    RUN_TEST(test_the_hooks_refusal_is_the_answer);
    RUN_TEST(test_a_hook_reading_past_its_args_is_bad_argc);
    RUN_TEST(test_a_reply_that_overflows_is_too_long);
    RUN_TEST(test_a_call_over_the_budget_counts_one_overrun);
    RUN_TEST(test_a_call_at_the_budget_counts_none);
    RUN_TEST(test_a_refused_call_counts_no_overrun);
    RUN_TEST(test_overruns_through_app_cmd_stop_at_0xffff);
    RUN_TEST(test_a_call_across_the_millisecond_wrap_is_timed_by_difference);
    RUN_TEST(test_a_slow_call_the_hook_refuses_counts_one_overrun);
    RUN_TEST(test_app_cmd_and_run_share_the_overrun_count);
    RUN_TEST(test_session_ok_follows_the_auth_gate);
    return UNITY_END();
}
