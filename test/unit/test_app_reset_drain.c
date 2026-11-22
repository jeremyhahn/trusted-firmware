/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What src/caps/sys.c's tfw_sys_poll promises the application at a RESET: no
 * stop hook runs and nothing is armed until the reply has drained, and the
 * last RESET accepted decides the stop, so a RESET(0) that supersedes a
 * RESET(1) is a plain reset to the application (shutdown only, no update
 * request) and a RESET(1) that supersedes a RESET(0) is an update.
 *
 * A byte left in the mock's TX queue stands for the RESET reply still on the
 * wire: tfw_hal_uart_tx_idle() is false until it is drained.
 */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "app_runner.h"
#include "app_mock.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/boot.h"

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_mock_app_reset();
}
void tearDown(void) {}

/* Accept a RESET(mode) without the tfw_sys_poll that carries it out. */
static void reset_cmd(uint8_t mode)
{
    inbuf[0] = mode;
    tfw_reader_init(&r, inbuf, 1u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_dispatch(TFW_CMD_RESET, &r, &w));
}

static uint8_t handoff_touched(void)
{
    uint8_t i;

    for (i = 0u; i < TFW_BOOT_REQUEST_LEN; i++) {
        if (tfw_mock_handoff()[i] != 0u) {
            return 1u;
        }
    }
    return 0u;
}

static void drain_all(void)
{
    uint8_t out[16];

    while (tfw_mock_tx_pending() != 0u) {
        (void)tfw_mock_drain_tx(out, sizeof out);
    }
}

static void test_a_reset_waits_for_its_reply_to_drain(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x55u));
    reset_cmd(0u);
    tfw_sys_poll();
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_STRING("ir", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());

    drain_all();
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_STRING("irs", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_STOPPED, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

static void test_a_mode_1_reset_arms_nothing_until_its_reply_drains(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x55u));
    reset_cmd(1u);
    tfw_sys_poll();
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_STRING("ir", tfw_mock_app_log());
    TEST_ASSERT_FALSE(handoff_touched());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());

    drain_all();
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_STRING("irbs", tfw_mock_app_log());
    TEST_ASSERT_TRUE(handoff_touched());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

static void test_a_reset_0_that_supersedes_a_reset_1_is_a_plain_reset_to_the_application(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();
    reset_cmd(1u);
    reset_cmd(0u);
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_STRING("irs", tfw_mock_app_log());
    TEST_ASSERT_FALSE(handoff_touched());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

static void test_a_reset_1_that_supersedes_a_reset_0_is_an_update_to_the_application(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();
    reset_cmd(0u);
    reset_cmd(1u);
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_STRING("irbs", tfw_mock_app_log());
    TEST_ASSERT_TRUE(handoff_touched());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_reset_waits_for_its_reply_to_drain);
    RUN_TEST(test_a_mode_1_reset_arms_nothing_until_its_reply_drains);
    RUN_TEST(test_a_reset_0_that_supersedes_a_reset_1_is_a_plain_reset_to_the_application);
    RUN_TEST(test_a_reset_1_that_supersedes_a_reset_0_is_an_update_to_the_application);
    return UNITY_END();
}
