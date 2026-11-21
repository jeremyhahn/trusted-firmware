/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The build with no application: the host library linked with
 * src/core/app_runner_none.c in place of the runner and the recording mock,
 * which is what every build without APP_DIR links (mk/app.mk). That it links
 * at all is half the point -- no descriptor, hook or configuration code is in
 * it, and nothing asks for one.
 */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "app_runner.h"
#include "attest.h"
#include "boot_soft_mock.h"
#include "hal_mock.h"
#include "tfw/config.h"
#include "tfw/protocol.h"

static const uint8_t UDS[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};
static const uint8_t FW[32] = {
    0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f,
    0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0,
};

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

/* With no application there is no evidence, and the buffer is left as it was. */
static void test_the_empty_runner_supplies_no_evidence(void)
{
    uint8_t out[TFW_APP_EVIDENCE_LEN];
    uint8_t was[TFW_APP_EVIDENCE_LEN];

    tfw_app_runner_start();
    memset(out, 0x5A, sizeof out);
    memset(was, 0x5A, sizeof was);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_app_runner_evidence(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(was, out, sizeof out);
}

/* No application, nothing to refuse: a trial without one runs on. */
static void test_the_empty_runner_never_refuses_a_trial(void)
{
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_app_runner_after_update());
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

/* Claim 273 with no application: the empty runner has no evidence, so the
 * image attests the firmware hash (attest.c), not what the buffer held --
 * running, and after the runner is stopped. */
static void test_without_an_application_the_measurement_is_the_firmware_hash(void)
{
    uint8_t out[TFW_SHA256_LEN];

    tfw_mock_boot_soft(UDS, FW);
    tfw_app_runner_start();
    memset(out, 0x5A, sizeof out);
    tfw_attest_measurement(out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FW, out, sizeof out);

    tfw_app_runner_stop(1u);
    memset(out, 0x5A, sizeof out);
    tfw_attest_measurement(out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FW, out, sizeof out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_empty_runner_stays_idle);
    RUN_TEST(test_the_empty_runner_supplies_no_evidence);
    RUN_TEST(test_the_empty_runner_never_refuses_a_trial);
    RUN_TEST(test_a_reset_still_resets);
    RUN_TEST(test_app_info_is_unknown_without_an_application);
    RUN_TEST(test_without_an_application_the_measurement_is_the_firmware_hash);
    return UNITY_END();
}
