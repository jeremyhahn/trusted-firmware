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
#include "vectors.h"

_Static_assert(sizeof TFWV_APPINFO_OVERRUN_REPLY - 1u == TFWV_APPINFO_OVERRUN_REPLY_LEN,
               "the OVERRUN literal is its stated length");
_Static_assert(sizeof TFWV_APPINFO_REFUSED_REPLY - 1u == TFWV_APPINFO_REFUSED_REPLY_LEN,
               "the REFUSED literal is its stated length");
_Static_assert(TFWV_APPINFO_OVERRUN_REPLY_LEN == TFW_APP_INFO_LEN, "APP_INFO is 63 bytes");

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

/* OTA_ACTIVATE mode 1's reset installs the marked image: an update too. */
static void test_an_install_reset_calls_before_update_then_shutdown(void)
{
    tfw_app_runner_start();
    tfw_app_runner_poll();
    tfw_sys_reset_soon();
    tfw_sys_poll();
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

/* ---- APP_INFO ---- */

static tfw_status_t app_info(uint8_t payload_len)
{
    inbuf[0] = 0u;
    tfw_reader_init(&r, inbuf, payload_len);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(TFW_CMD_APP_INFO, &r, &w);
}

static void test_app_info_reports_the_runner_and_the_descriptor(void)
{
    uint32_t i;

    tfw_mock_app_set_run_ms(TFW_APP_RUN_BUDGET_MS + 1u);
    tfw_app_runner_start();
    for (i = 0u; i < 0x0102u; i++) {
        tfw_app_runner_poll();
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_info(0u));
    TEST_ASSERT_EQUAL_UINT8(TFWV_APPINFO_OVERRUN_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_APPINFO_OVERRUN_REPLY, outbuf,
                                 TFWV_APPINFO_OVERRUN_REPLY_LEN);
}

/* The host diagnosing an application that never started needs what was
 * refused, so a refused descriptor is reported as written. */
static void test_app_info_reports_a_refused_descriptor_as_written(void)
{
    tfw_app_descriptor_t *d = tfw_mock_app_descriptor();

    d->ota_slots     = TFW_APP_OTA_AB;
    d->version_major = 2u;
    d->version_minor = 3u;
    d->version_patch = 1029u;
    d->config_len    = 0x0102u;
    d->required_caps = TFW_APP_CAP(31u) | TFW_APP_CAP(0u);
    memcpy(d->name, "abcdefghijklmno", TFW_APP_NAME_LEN);
    memcpy(d->profile, "bmc/esp32-p4", 13u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_INT(TFW_OK, app_info(0u));
    TEST_ASSERT_EQUAL_UINT8(TFWV_APPINFO_REFUSED_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_APPINFO_REFUSED_REPLY, outbuf,
                                 TFWV_APPINFO_REFUSED_REPLY_LEN);
}

static void test_app_info_takes_no_payload(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, app_info(1u));
}

/* The evidence a running application supplies is handed on byte for byte. */
static void test_a_running_application_supplies_its_evidence(void)
{
    static const uint8_t EV[] = TFWV_APPEV_EXAMPLE;
    uint8_t out[TFW_APP_EVIDENCE_LEN];

    tfw_mock_app_set_evidence(TFW_OK, EV);
    tfw_app_runner_start();
    memset(out, 0, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_app_runner_evidence(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EV, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ie", tfw_mock_app_log());
}

/* An application that declines supplies nothing, even when it wrote into the
 * buffer before it declined: the caller's bytes are as they were, never the
 * half the application left. */
static void test_an_application_that_declines_leaves_the_buffer_as_it_was(void)
{
    static const uint8_t EV[] = TFWV_APPEV_EXAMPLE;
    uint8_t out[TFW_APP_EVIDENCE_LEN];
    uint8_t was[TFW_APP_EVIDENCE_LEN];

    tfw_mock_app_set_evidence(TFW_ERR_BAD_STATE, EV);
    tfw_app_runner_start();
    memset(out, 0x5A, sizeof out);
    memset(was, 0x5A, sizeof was);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_app_runner_evidence(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(was, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ie", tfw_mock_app_log());
}

/* Only a running application is asked: not one that failed its init, was
 * stopped or was refused. */
static void evidence_is_not_asked(const char *log)
{
    static const uint8_t EV[] = TFWV_APPEV_EXAMPLE;
    uint8_t out[TFW_APP_EVIDENCE_LEN];
    uint8_t was[TFW_APP_EVIDENCE_LEN];

    tfw_mock_app_set_evidence(TFW_OK, EV);
    memset(out, 0x5A, sizeof out);
    memset(was, 0x5A, sizeof was);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_app_runner_evidence(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(was, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING(log, tfw_mock_app_log());
}

static void test_a_failed_application_is_not_asked_for_evidence(void)
{
    tfw_mock_app_set_init(TFW_ERR_TIMEOUT);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_FAILED, tfw_app_runner_state());
    evidence_is_not_asked("i");
}

static void test_a_stopped_application_is_not_asked_for_evidence(void)
{
    tfw_app_runner_start();
    tfw_app_runner_stop(0u);
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_STOPPED, tfw_app_runner_state());
    evidence_is_not_asked("is");
}

static void test_a_refused_application_is_not_asked_for_evidence(void)
{
    tfw_mock_app_descriptor()->magic = TFW_APP_MAGIC ^ 1u;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
    evidence_is_not_asked("");
}

/* The trial's question (src/core/ota_trial.c): a running application's
 * after_update answer is passed through as it is, both ways. */
static void test_a_running_application_answers_the_trial(void)
{
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_app_runner_after_update());
    TEST_ASSERT_EQUAL_STRING("ia", tfw_mock_app_log());
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

static void test_a_running_application_can_refuse_the_trial(void)
{
    tfw_mock_app_set_after_update(TFW_ERR_BAD_STATE);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_app_runner_after_update());
    TEST_ASSERT_EQUAL_STRING("ia", tfw_mock_app_log());
}

/* Only the hook refuses: an application whose init refused, or whose
 * descriptor was refused, is not asked and the trial runs on. */
static void test_an_application_that_is_not_running_is_not_asked_by_the_trial(void)
{
    tfw_mock_app_set_after_update(TFW_ERR_BAD_STATE);
    tfw_mock_app_set_init(TFW_ERR_TIMEOUT);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_app_runner_after_update());
    TEST_ASSERT_EQUAL_STRING("i", tfw_mock_app_log());

    tfw_mock_app_reset();
    tfw_mock_app_set_after_update(TFW_ERR_BAD_STATE);
    tfw_mock_app_descriptor()->magic = TFW_APP_MAGIC ^ 1u;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_app_runner_after_update());
    TEST_ASSERT_EQUAL_STRING("", tfw_mock_app_log());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_calls_init_and_poll_calls_run);
    RUN_TEST(test_run_is_given_the_time_of_the_pass);
    RUN_TEST(test_reset_stops_the_application_before_the_device_resets);
    RUN_TEST(test_reset_to_bootloader_calls_before_update_then_shutdown);
    RUN_TEST(test_an_install_reset_calls_before_update_then_shutdown);
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
    RUN_TEST(test_app_info_reports_the_runner_and_the_descriptor);
    RUN_TEST(test_app_info_reports_a_refused_descriptor_as_written);
    RUN_TEST(test_app_info_takes_no_payload);
    RUN_TEST(test_a_running_application_supplies_its_evidence);
    RUN_TEST(test_an_application_that_declines_leaves_the_buffer_as_it_was);
    RUN_TEST(test_a_failed_application_is_not_asked_for_evidence);
    RUN_TEST(test_a_stopped_application_is_not_asked_for_evidence);
    RUN_TEST(test_a_refused_application_is_not_asked_for_evidence);
    RUN_TEST(test_a_running_application_answers_the_trial);
    RUN_TEST(test_a_running_application_can_refuse_the_trial);
    RUN_TEST(test_an_application_that_is_not_running_is_not_asked_by_the_trial);
    return UNITY_END();
}
