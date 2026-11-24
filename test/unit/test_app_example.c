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

static void test_the_heartbeat_keeps_its_period_across_the_millisecond_wrap(void)
{
    uint8_t lit;

    /* The heartbeat's schedule outlives the mock's reset between cases, so
     * bring it to the wrap in jumps under 2^31: each poll is due and beats. */
    tfw_app_runner_start();
    tfw_mock_set_millis(0x7FFFFF00u);
    tfw_app_runner_poll();
    tfw_mock_set_millis(0xFFFFFF38u);
    tfw_app_runner_poll();                 /* the next beat wraps to 0x12C */
    lit = led_level();

    tfw_mock_set_millis(0xFFFFFFFFu);
    tfw_app_runner_poll();                 /* numerically past 0x12C, not due */
    TEST_ASSERT_EQUAL_UINT8(lit, led_level());

    tfw_mock_set_millis(0x0000012Bu);
    tfw_app_runner_poll();                 /* 1 ms short of the period */
    TEST_ASSERT_EQUAL_UINT8(lit, led_level());

    tfw_mock_set_millis(0x0000012Cu);
    tfw_app_runner_poll();                 /* 500 ms after the last beat */
    TEST_ASSERT_EQUAL_UINT8((uint8_t)!lit, led_level());
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

/* The example supplies the evidence the with-claim vector pins. */
static void test_the_example_supplies_its_evidence(void)
{
    static const uint8_t EV[] = TFWV_APPEV_EXAMPLE;
    uint8_t out[TFW_APP_EVIDENCE_LEN];

    tfw_app_runner_start();
    memset(out, 0, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_app_runner_evidence(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EV, out, sizeof out);
}

/* A pin the HAL refuses makes the example's init refuse: the runner leaves it
 * FAILED, never runs it and asks it for no evidence, and the next start, with
 * the HAL accepting, runs it. Run last: it never runs the heartbeat. */
static void test_a_pin_the_hal_refuses_leaves_the_heartbeat_failed_and_dark(void)
{
    uint8_t out[TFW_APP_EVIDENCE_LEN];

    tfw_mock_gpio_mode_fails_once(TFW_ERR_UNSUPPORTED);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_FAILED, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_is_output(led.port, led.bit));

    tfw_mock_advance(500u);
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, led_level());
    tfw_mock_advance(500u);
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, led_level());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_app_runner_overruns());

    memset(out, 0x5A, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_app_runner_evidence(out));
    TEST_ASSERT_EACH_EQUAL_HEX8(0x5Au, out, sizeof out);

    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_is_output(led.port, led.bit));
}

/* One APP_CMD request through the dispatcher, as the wire delivers it. */
static tfw_status_t app_cmd(const char *req, uint8_t req_len, uint8_t *out, uint8_t *out_len)
{
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    tfw_reader_init(&r, (const uint8_t *)req, req_len);
    tfw_writer_init(&w, out, TFW_MAX_PAYLOAD);
    st       = tfw_dispatch(TFW_CMD_APP_CMD, &r, &w);
    *out_len = tfw_writer_len(&w);
    return st;
}

/* Three toggles 500 ms apart, then op 0x00 answers the count. */
static void test_the_blink_count_answers_the_toggles_so_far(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len = 0u;

    /* The schedule outlives the mock's reset between cases (see the wrap case),
     * so jump well past any beat a prior case left due: the first poll beats. */
    tfw_app_runner_start();
    tfw_mock_set_millis(0x00100000u);
    for (uint8_t i = 0u; i < 3u; i++) {
        if (i != 0u) {
            tfw_mock_advance(500u);
        }
        tfw_app_runner_poll();
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_cmd(TFWV_APPCMD_BLINK_REQUEST, TFWV_APPCMD_BLINK_REQUEST_LEN,
                                          out, &len));
    TEST_ASSERT_EQUAL_UINT8(TFWV_APPCMD_BLINK_REPLY_LEN, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_APPCMD_BLINK_REPLY, out,
                                 TFWV_APPCMD_BLINK_REPLY_LEN);
}

static void test_the_blink_count_refuses_a_trailing_byte(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len = 0u;

    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFWV_APPCMD_TRAILING_STATUS,
                          app_cmd(TFWV_APPCMD_TRAILING_REQUEST, TFWV_APPCMD_TRAILING_REQUEST_LEN,
                                  out, &len));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, TFWV_APPCMD_TRAILING_STATUS);
}

static void test_an_op_the_example_does_not_define_is_unsupported(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len = 0u;

    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFWV_APPCMD_UNKNOWN_STATUS,
                          app_cmd(TFWV_APPCMD_UNKNOWN_REQUEST, TFWV_APPCMD_UNKNOWN_REQUEST_LEN,
                                  out, &len));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, TFWV_APPCMD_UNKNOWN_STATUS);
}

/* Not started: the runner answers before the example is called. */
static void test_the_blink_count_needs_a_running_heartbeat(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len = 0u;

    tfw_app_runner_reset_state();          /* idle: a prior test left it running */
    TEST_ASSERT_EQUAL_INT(TFWV_APPCMD_STOPPED_STATUS,
                          app_cmd(TFWV_APPCMD_BLINK_REQUEST, TFWV_APPCMD_BLINK_REQUEST_LEN,
                                  out, &len));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, TFWV_APPCMD_STOPPED_STATUS);
    TEST_ASSERT_EQUAL_UINT8(0u, len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_example_names_itself);
    RUN_TEST(test_the_heartbeat_starts_with_its_pin_an_output);
    RUN_TEST(test_the_heartbeat_toggles_every_500_ms_and_never_waits);
    RUN_TEST(test_the_heartbeat_keeps_its_period_across_the_millisecond_wrap);
    RUN_TEST(test_a_reset_leaves_the_pin_low);
    RUN_TEST(test_app_info_reports_the_example);
    RUN_TEST(test_the_example_supplies_its_evidence);
    RUN_TEST(test_a_pin_the_hal_refuses_leaves_the_heartbeat_failed_and_dark);
    RUN_TEST(test_the_blink_count_answers_the_toggles_so_far);
    RUN_TEST(test_the_blink_count_refuses_a_trailing_byte);
    RUN_TEST(test_an_op_the_example_does_not_define_is_unsupported);
    RUN_TEST(test_the_blink_count_needs_a_running_heartbeat);
    return UNITY_END();
}
