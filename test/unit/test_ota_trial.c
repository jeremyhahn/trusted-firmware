/* SPDX-License-Identifier: commercial */
/*
 * The application's half of the one trial (src/core/ota_trial.c), on the mock
 * HAL's clock: in TRIAL the watchdog is re-armed and petted, the deadline
 * resets, the stop ends the count, and any other state arms nothing.
 */

#include <string.h>

#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_platform.h"
#include "ota_trial.h"

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

void setUp(void)    { tfw_mock_reset(); }
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

static void test_the_stop_ends_the_count_and_keeps_petting(void)
{
    provision(TFW_BOOT_STATE_TRIAL);
    tfw_ota_trial_start();
    tfw_ota_trial_stop();

    tfw_mock_advance(DEADLINE_MS * 2u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_watchdog_pets());
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_trial_boot_rearms_and_pets);
    RUN_TEST(test_the_deadline_resets_and_not_before);
    RUN_TEST(test_the_stop_ends_the_count_and_keeps_petting);
    RUN_TEST(test_any_other_state_arms_nothing);
    RUN_TEST(test_no_platform_block_arms_nothing);
    return UNITY_END();
}
