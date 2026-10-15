/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application runner (src/core/app_runner.c) against the recording
 * application of test/mock/app_mock.c: the order the hooks are called in, what
 * a failing init and each refused descriptor leave behind, the run budget's
 * count, and the bounds of the configuration region.
 *
 * Each case drives the device the way it is driven: start is what startup()
 * calls, poll is one superloop pass, and a stop is a RESET command followed by
 * the tfw_sys_poll that carries it out.
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

/* A RESET in the given mode, and the poll that performs it. */
static void reset_with(uint8_t mode)
{
    inbuf[0] = mode;
    tfw_reader_init(&r, inbuf, 1u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_dispatch(TFW_CMD_RESET, &r, &w));
    tfw_sys_poll();
}

static void assert_refused(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();
    reset_with(0u);
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_STRING("", tfw_mock_app_log());
}

/* ------------------------------------------------------------ hook order */

static void test_start_calls_init_and_poll_calls_run(void)
{
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
    tfw_app_runner_poll();
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_STRING("irr", tfw_mock_app_log());
}

static void test_run_is_given_the_time_of_the_pass(void)
{
    tfw_mock_set_millis(12345u);
    tfw_app_runner_start();
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT32(12345u, tfw_mock_app_last_now());
}

static void test_reset_stops_the_application_before_the_device_resets(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();
    reset_with(0u);
    TEST_ASSERT_EQUAL_STRING("irs", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_STOPPED, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

/* Mode 1 is the update path: before_update, then the shutdown every stop has. */
static void test_reset_to_bootloader_calls_before_update_then_shutdown(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();
    reset_with(1u);
    TEST_ASSERT_EQUAL_STRING("irbs", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

static void test_a_stopped_application_is_not_run_or_stopped_again(void)
{
    tfw_app_runner_start();
    tfw_app_runner_stop(0u);
    tfw_app_runner_stop(1u);
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_STRING("is", tfw_mock_app_log());
}

/* ------------------------------------------------------- a failing init */

/* The firmware serves on without it: no run, and no shutdown of an application
 * that never started. */
static void test_a_failing_init_leaves_the_application_failed_and_uncalled(void)
{
    tfw_mock_app_set_init(TFW_ERR_TIMEOUT);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_FAILED, tfw_app_runner_state());
    tfw_app_runner_poll();
    reset_with(1u);
    TEST_ASSERT_EQUAL_STRING("i", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_FAILED, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

/* ------------------------------------------------- refused descriptors */

static void test_a_wrong_magic_is_refused(void)
{
    tfw_mock_app_descriptor()->magic = TFW_APP_MAGIC ^ 1u;
    assert_refused();
}

static void test_a_newer_abi_is_refused(void)
{
    tfw_mock_app_descriptor()->abi = (uint8_t)(TFW_APP_ABI + 1u);
    assert_refused();
}

static void test_an_empty_name_is_refused(void)
{
    tfw_mock_app_descriptor()->name[0] = '\0';
    assert_refused();
}

static void test_a_name_with_no_terminator_is_refused(void)
{
    memset(tfw_mock_app_descriptor()->name, 'a', TFW_APP_NAME_LEN);
    assert_refused();
}

static void test_a_profile_with_no_terminator_is_refused(void)
{
    memset(tfw_mock_app_descriptor()->profile, 'p', TFW_APP_PROFILE_LEN);
    assert_refused();
}

static void test_a_name_filling_its_field_but_terminated_is_accepted(void)
{
    memset(tfw_mock_app_descriptor()->name, 'a', TFW_APP_NAME_LEN - 1u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

/* Bit 31 names no capability; a capability the image has is accepted. */
static void test_a_capability_this_image_lacks_is_refused(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)TFW_FEATURE_BITMAP & TFW_APP_CAP(31u));
    tfw_mock_app_descriptor()->required_caps = TFW_APP_CAP(31u);
    assert_refused();
}

static void test_a_capability_this_image_has_is_accepted(void)
{
    tfw_mock_app_descriptor()->required_caps = TFW_APP_CAP(TFW_CAPBIT_GPIO);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

static void test_more_update_slots_than_the_board_has_are_refused(void)
{
    tfw_mock_app_descriptor()->ota_slots = (uint8_t)(TFW_OTA_SLOTS + 1u);
    assert_refused();
}

static void test_the_slots_the_board_has_are_accepted(void)
{
    tfw_mock_app_descriptor()->ota_slots = (uint8_t)TFW_OTA_SLOTS;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

static void test_configuration_past_the_end_of_the_eeprom_is_refused(void)
{
    tfw_mock_app_descriptor()->config_len =
        (uint16_t)(TFW_EEPROM_SIZE - TFW_APP_CONFIG_BASE + 1u);
    assert_refused();
}

static void test_configuration_to_the_end_of_the_eeprom_is_accepted(void)
{
    tfw_mock_app_descriptor()->config_len = (uint16_t)(TFW_EEPROM_SIZE - TFW_APP_CONFIG_BASE);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

/* A start after a refusal is a fresh start: nothing is carried over. */
static void test_a_refusal_does_not_outlive_the_descriptor_that_caused_it(void)
{
    tfw_mock_app_descriptor()->magic = 0u;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
    tfw_mock_app_reset();
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

/* ------------------------------------------------------------ run budget */

static void test_a_run_within_the_budget_is_not_counted(void)
{
    tfw_mock_app_set_run_ms(TFW_APP_RUN_BUDGET_MS);
    tfw_app_runner_start();
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_app_runner_overruns());
}

/* Counted, not enforced: the slow application still ran, and runs again. */
static void test_a_run_over_the_budget_is_counted_and_not_stopped(void)
{
    tfw_mock_app_set_run_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    tfw_app_runner_poll();
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT16(2u, tfw_app_runner_overruns());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_STRING("irr", tfw_mock_app_log());
}

static void test_the_overrun_count_saturates(void)
{
    uint32_t i;

    tfw_mock_app_set_run_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    for (i = 0u; i < 0x10001uL; i++) {
        tfw_app_runner_poll();
    }
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, tfw_app_runner_overruns());
}

static void test_a_start_clears_the_overrun_count(void)
{
    tfw_mock_app_set_run_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    tfw_app_runner_poll();
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_app_runner_overruns());
}

/* ---------------------------------------------------- configuration region */

static void test_configuration_lands_after_the_boot_block(void)
{
    const uint8_t in[3] = { 0x11u, 0x22u, 0x33u };
    uint8_t       out[3];

    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_app_config_write(13u, in, 3u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, &tfw_mock_eeprom()[TFW_APP_CONFIG_BASE + 13u], 3);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_app_config_read(13u, out, 3u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, 3);
}

static void test_configuration_past_the_declared_length_is_out_of_range(void)
{
    uint8_t buf[4] = { 0u, 0u, 0u, 0u };

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_write(13u, buf, 4u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_read(16u, buf, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[TFW_APP_CONFIG_BASE + 13u]);
}

static void test_an_empty_access_at_the_end_is_allowed_and_past_it_is_not(void)
{
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_app_config_read(16u, &b, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_read(17u, &b, 0u));
}

/* A refused application may still call these; its overlong length does not
 * reach past the part. */
static void test_configuration_never_reaches_past_the_eeprom(void)
{
    uint8_t b = 0u;

    tfw_mock_app_descriptor()->config_len = 0xFFFFu;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_app_config_write((uint16_t)(TFW_EEPROM_SIZE - TFW_APP_CONFIG_BASE), &b, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_calls_init_and_poll_calls_run);
    RUN_TEST(test_run_is_given_the_time_of_the_pass);
    RUN_TEST(test_reset_stops_the_application_before_the_device_resets);
    RUN_TEST(test_reset_to_bootloader_calls_before_update_then_shutdown);
    RUN_TEST(test_a_stopped_application_is_not_run_or_stopped_again);
    RUN_TEST(test_a_failing_init_leaves_the_application_failed_and_uncalled);
    RUN_TEST(test_a_wrong_magic_is_refused);
    RUN_TEST(test_a_newer_abi_is_refused);
    RUN_TEST(test_an_empty_name_is_refused);
    RUN_TEST(test_a_name_with_no_terminator_is_refused);
    RUN_TEST(test_a_profile_with_no_terminator_is_refused);
    RUN_TEST(test_a_name_filling_its_field_but_terminated_is_accepted);
    RUN_TEST(test_a_capability_this_image_lacks_is_refused);
    RUN_TEST(test_a_capability_this_image_has_is_accepted);
    RUN_TEST(test_more_update_slots_than_the_board_has_are_refused);
    RUN_TEST(test_the_slots_the_board_has_are_accepted);
    RUN_TEST(test_configuration_past_the_end_of_the_eeprom_is_refused);
    RUN_TEST(test_configuration_to_the_end_of_the_eeprom_is_accepted);
    RUN_TEST(test_a_refusal_does_not_outlive_the_descriptor_that_caused_it);
    RUN_TEST(test_a_run_within_the_budget_is_not_counted);
    RUN_TEST(test_a_run_over_the_budget_is_counted_and_not_stopped);
    RUN_TEST(test_the_overrun_count_saturates);
    RUN_TEST(test_a_start_clears_the_overrun_count);
    RUN_TEST(test_configuration_lands_after_the_boot_block);
    RUN_TEST(test_configuration_past_the_declared_length_is_out_of_range);
    RUN_TEST(test_an_empty_access_at_the_end_is_allowed_and_past_it_is_not);
    RUN_TEST(test_configuration_never_reaches_past_the_eeprom);
    return UNITY_END();
}
