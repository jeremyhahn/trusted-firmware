/* SPDX-License-Identifier: commercial */
/*
 * The application's half of the one trial (src/core/ota_trial.c), on the mock
 * HAL's clock: in TRIAL the watchdog is re-armed and petted, the deadline
 * resets, a confirm request stops nothing, and any other state arms nothing.
 * After the runner's start, the trial asks after_update once (ADR decision 9):
 * a refusal resets at once, TFW_OK leaves the deadline running, and outside
 * TRIAL nothing is asked.
 */

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_platform.h"
#include "crc16.h"
#include "ota_trial.h"
#include "runner_fake.h"

#define DEADLINE_MS ((tfw_ms_t)TFW_OTA_TRIAL_TIMEOUT_S * 1000u)

static void provision(uint8_t boot_state)
{
    tfw_boot_platform_t p;
    uint8_t             raw[TFW_BOOT_EE_LEN];

    memset(&p, 0, sizeof p);
    memset(raw, 0, sizeof raw);
    p.boot_state = boot_state;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN));
}

void setUp(void)    { tfw_mock_reset(); tfw_fake_runner_reset(); }
void tearDown(void) { }

static void test_a_trial_boot_rearms_and_pets(void)
{
    provision(TFW_BOOT_STATE_TRIAL);
    tfw_ota_trial_start();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_watchdog_arms());

    tfw_ota_trial_poll();
    tfw_mock_advance(1000u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(2u, tfw_mock_watchdog_pets());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

static void test_the_deadline_resets_and_not_before(void)
{
    tfw_mock_set_millis(500u);
    provision(TFW_BOOT_STATE_TRIAL);
    tfw_ota_trial_start();

    tfw_mock_advance(DEADLINE_MS - 1u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());

    tfw_mock_advance(1u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

/* OTA_CONFIRM writes its request and leaves the count alone (#137): there is
 * no call that stops it, so a confirmed trial is petted up to the deadline and
 * reset there like any other, the request left in place for tfw-boot to
 * settle it from. */
static void test_a_confirmed_trial_still_resets_at_the_deadline(void)
{
    uint8_t *rec = &tfw_mock_eeprom()[TFW_BOOT_CONFIRM_ADDR];
    uint8_t  want[TFW_BOOT_CONFIRM_LEN];
    uint16_t crc;

    provision(TFW_BOOT_STATE_TRIAL);
    rec[0] = TFW_BOOT_CONFIRM_MAGIC;
    memset(&rec[TFW_BOOT_CONFIRM_O_MEAS], 0x3Cu, TFW_BOOT_CONFIRM_MEAS_LEN);
    crc = tfw_crc16(rec, TFW_BOOT_CONFIRM_O_CRC);
    rec[TFW_BOOT_CONFIRM_O_CRC]      = (uint8_t)crc;
    rec[TFW_BOOT_CONFIRM_O_CRC + 1u] = (uint8_t)(crc >> 8);
    memcpy(want, rec, sizeof want);
    tfw_ota_trial_start();

    tfw_mock_advance(DEADLINE_MS / 2u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    tfw_mock_advance(DEADLINE_MS);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_EQUAL_UINT(2u, tfw_mock_watchdog_pets());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, rec, TFW_BOOT_CONFIRM_LEN);   /* left for tfw-boot */
}

static void check_arms_nothing(uint8_t boot_state)
{
    tfw_mock_reset();
    provision(boot_state);
    tfw_ota_trial_start();
    tfw_mock_advance(DEADLINE_MS);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_watchdog_arms());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_watchdog_pets());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

static void test_any_other_state_arms_nothing(void)
{
    check_arms_nothing(TFW_BOOT_STATE_EMPTY);
    check_arms_nothing(TFW_BOOT_STATE_STAGED);
    check_arms_nothing(TFW_BOOT_STATE_GOOD);
    check_arms_nothing(TFW_BOOT_STATE_CONFIRMED);
}

/* Erased EEPROM: no platform block, so no trial. */
static void test_no_platform_block_arms_nothing(void)
{
    tfw_ota_trial_start();
    tfw_mock_advance(DEADLINE_MS);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_watchdog_arms());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

/* TFW_OK confirms nothing: the trial runs on to its deadline, as an
 * unconfirmed one does, and the control plane's OTA_CONFIRM settles it. */
static void test_an_accepted_after_update_leaves_the_trial_running(void)
{
    provision(TFW_BOOT_STATE_TRIAL);
    tfw_ota_trial_start();
    tfw_ota_trial_after_init();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_fake_runner_after_update_calls());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());

    tfw_mock_advance(DEADLINE_MS - 1u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    tfw_mock_advance(1u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_fake_runner_after_update_calls());
}

/* A refusal ends the trial at once, as failed: the reset lands before any
 * poll, with no time passed, and tfw-boot finds TRIAL with no confirm. */
static void test_a_refused_after_update_ends_the_trial_at_once(void)
{
    tfw_fake_runner_set_after_update(TFW_ERR_BAD_STATE);
    provision(TFW_BOOT_STATE_TRIAL);
    tfw_ota_trial_start();
    tfw_ota_trial_after_init();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_fake_runner_after_update_calls());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

static void check_not_asked(uint8_t boot_state)
{
    tfw_mock_reset();
    tfw_fake_runner_reset();
    tfw_fake_runner_set_after_update(TFW_ERR_BAD_STATE);
    provision(boot_state);
    tfw_ota_trial_start();
    tfw_ota_trial_after_init();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_fake_runner_after_update_calls());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

/* Not a trial boot, so not a newly installed image: after_update is not
 * asked, and a refusal it would have given resets nothing. */
static void test_outside_a_trial_after_update_is_not_asked(void)
{
    check_not_asked(TFW_BOOT_STATE_EMPTY);
    check_not_asked(TFW_BOOT_STATE_STAGED);
    check_not_asked(TFW_BOOT_STATE_GOOD);
    check_not_asked(TFW_BOOT_STATE_CONFIRMED);

    tfw_mock_reset();
    tfw_fake_runner_reset();
    tfw_ota_trial_start();                 /* erased EEPROM: no block */
    tfw_ota_trial_after_init();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_fake_runner_after_update_calls());
}

/* Open question 1, option A: an application that cannot start ends the trial
 * at once, by the refusal's reset, and after_update is never asked. */
static void check_cannot_start_ends_the_trial(tfw_app_state_t app)
{
    tfw_mock_reset();
    tfw_fake_runner_reset();
    tfw_fake_runner_set_state(app);
    provision(TFW_BOOT_STATE_TRIAL);
    tfw_ota_trial_start();
    tfw_ota_trial_after_init();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_fake_runner_after_update_calls());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

static void test_a_failed_init_ends_the_trial_at_once(void)
{
    check_cannot_start_ends_the_trial(TFW_APP_STATE_FAILED);
}

static void test_an_unsupported_descriptor_ends_the_trial_at_once(void)
{
    check_cannot_start_ends_the_trial(TFW_APP_STATE_UNSUPPORTED);
}

/* No application (app_runner_none.c reports IDLE) is not a failed init: the
 * trial runs on to OTA_CONFIRM or its deadline. */
static void test_no_application_leaves_the_trial_running(void)
{
    tfw_fake_runner_set_state(TFW_APP_STATE_IDLE);
    provision(TFW_BOOT_STATE_TRIAL);
    tfw_ota_trial_start();
    tfw_ota_trial_after_init();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_fake_runner_after_update_calls());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

/* Outside a trial a failed init resets nothing: the firmware serves on, and
 * nothing the trial polls resets it later. */
static void check_failed_init_serves_on(uint8_t boot_state)
{
    tfw_mock_reset();
    tfw_fake_runner_reset();
    tfw_fake_runner_set_state(TFW_APP_STATE_FAILED);
    provision(boot_state);
    tfw_ota_trial_start();
    tfw_ota_trial_after_init();
    tfw_mock_advance(DEADLINE_MS);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_fake_runner_after_update_calls());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

static void test_outside_a_trial_a_failed_init_serves_on(void)
{
    check_failed_init_serves_on(TFW_BOOT_STATE_EMPTY);
    check_failed_init_serves_on(TFW_BOOT_STATE_STAGED);
    check_failed_init_serves_on(TFW_BOOT_STATE_GOOD);
    check_failed_init_serves_on(TFW_BOOT_STATE_CONFIRMED);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_trial_boot_rearms_and_pets);
    RUN_TEST(test_the_deadline_resets_and_not_before);
    RUN_TEST(test_a_confirmed_trial_still_resets_at_the_deadline);
    RUN_TEST(test_any_other_state_arms_nothing);
    RUN_TEST(test_no_platform_block_arms_nothing);
    RUN_TEST(test_an_accepted_after_update_leaves_the_trial_running);
    RUN_TEST(test_a_refused_after_update_ends_the_trial_at_once);
    RUN_TEST(test_outside_a_trial_after_update_is_not_asked);
    RUN_TEST(test_a_failed_init_ends_the_trial_at_once);
    RUN_TEST(test_an_unsupported_descriptor_ends_the_trial_at_once);
    RUN_TEST(test_no_application_leaves_the_trial_running);
    RUN_TEST(test_outside_a_trial_a_failed_init_serves_on);
    return UNITY_END();
}
