/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The build with no application: the host library linked with
 * src/core/app_runner_none.c in place of the runner and the recording mock,
 * which is what every build without APP_DIR links (mk/app.mk). That it links
 * at all is half the point -- no descriptor, hook or configuration code is in
 * it, and nothing asks for one.
 */

#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "app_runner.h"
#include "hal_mock.h"
#include "tfw/config.h"
#include "tfw/protocol.h"

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
}
void tearDown(void) {}

/* There is nothing to run, so the runner never leaves IDLE. */
static void test_the_empty_runner_stays_idle(void)
{
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_IDLE, tfw_app_runner_state());
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_IDLE, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_app_runner_overruns());
    tfw_app_runner_stop(1u);
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_IDLE, tfw_app_runner_state());
}

/* A commanded reset still resets: the stop it calls first is harmless. */
static void test_a_reset_still_resets(void)
{
    uint8_t      in[1] = { 0u };
    uint8_t      out[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_app_runner_start();
    tfw_reader_init(&r, in, 1u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_dispatch(TFW_CMD_RESET, &r, &w));
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_IDLE, tfw_app_runner_state());
}

/* An image with no application has no APP_INFO handler: it answers as for any
 * absent feature, so the no-application image is unchanged on the wire. */
static void test_app_info_is_unknown_without_an_application(void)
{
    uint8_t      in[1] = { 0u };
    uint8_t      out[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, 0u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNKNOWN_CMD, tfw_dispatch(TFW_CMD_APP_INFO, &r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_empty_runner_stays_idle);
    RUN_TEST(test_a_reset_still_resets);
    RUN_TEST(test_app_info_is_unknown_without_an_application);
    return UNITY_END();
}
