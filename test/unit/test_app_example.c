/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The example application (test/app/example/), compiled as an application is
 * -- against include/tfw only, see mk/host.mk -- and driven through the runner
 * on the mock HAL: the heartbeat toggles pin 13 every 500 ms, and a RESET
 * leaves it low.
 */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "app_runner.h"
#include "hal_mock.h"
#include "tfw/app.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "vectors.h"

_Static_assert(sizeof TFWV_APPINFO_HEARTBEAT_REPLY - 1u == TFWV_APPINFO_HEARTBEAT_REPLY_LEN,
               "the HEARTBEAT literal is its stated length");

static tfw_pinref_t led;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_board_pin(13u, &led));
}
void tearDown(void) {}

static uint8_t led_level(void)
{
    return tfw_mock_pin_level(led.port, led.bit);
}

static void test_the_example_names_itself(void)
{
    TEST_ASSERT_EQUAL_STRING("heartbeat", tfw_app_descriptor.name);
}

static void test_the_heartbeat_starts_with_its_pin_an_output(void)
{
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_is_output(led.port, led.bit));
}

/* The first case to run the heartbeat, so it sees the state a boot leaves. */
static void test_the_heartbeat_toggles_every_500_ms_and_never_waits(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();                 /* t=0: due at once */
    TEST_ASSERT_EQUAL_UINT8(1u, led_level());

    tfw_mock_advance(499u);
    tfw_app_runner_poll();                 /* not yet: returns at once */
    TEST_ASSERT_EQUAL_UINT8(1u, led_level());

    tfw_mock_advance(1u);
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, led_level());

    tfw_mock_advance(500u);
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, led_level());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_app_runner_overruns());
}

static void test_a_reset_leaves_the_pin_low(void)
{
    uint8_t      in[1] = { 0u };
    uint8_t      out[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;
    unsigned     i;

    /* The heartbeat's own state outlives the mock's reset between cases, as it
     * would not on a device that boots once, so beat until the pin is high. */
    tfw_app_runner_start();
    for (i = 0u; i < 8u && led_level() == 0u; i++) {
        tfw_mock_advance(500u);
        tfw_app_runner_poll();
    }
    TEST_ASSERT_EQUAL_UINT8(1u, led_level());

    tfw_reader_init(&r, in, 1u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_dispatch(TFW_CMD_RESET, &r, &w));
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, led_level());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_STOPPED, tfw_app_runner_state());
}

/* The vector tfwsim-app-heartbeat answers in sdk/go's cross-implementation
 * suite, pinned here against the same descriptor on the mock HAL. */
static void test_app_info_reports_the_example(void)
{
    uint8_t      in[1] = { 0u };
    uint8_t      out[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_app_runner_start();
    tfw_reader_init(&r, in, 0u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_dispatch(TFW_CMD_APP_INFO, &r, &w));
    TEST_ASSERT_EQUAL_UINT8(TFWV_APPINFO_HEARTBEAT_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_APPINFO_HEARTBEAT_REPLY, out,
                                 TFWV_APPINFO_HEARTBEAT_REPLY_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_example_names_itself);
    RUN_TEST(test_the_heartbeat_starts_with_its_pin_an_output);
    RUN_TEST(test_the_heartbeat_toggles_every_500_ms_and_never_waits);
    RUN_TEST(test_a_reset_leaves_the_pin_low);
    RUN_TEST(test_app_info_reports_the_example);
    return UNITY_END();
}
