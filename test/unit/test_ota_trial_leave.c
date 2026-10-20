/*
 * A trial whose platform block stops reading TRIAL before its deadline
 * (src/core/ota_trial.c, #163): every boot_state byte, and the block corrupted
 * bit by bit, written between tfw_ota_trial_start and the deadline. Same link
 * as test_ota_trial.
 *
 * The oracle is the deadline's rule: at the deadline a block that still reads
 * TRIAL resets; one that does not (an ESP32 confirm ended the trial, or the
 * block no longer parses) disarms without a reset, and nothing is petted after.
 * Before the deadline the block is not consulted: every poll pets.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_platform.h"
#include "ota_trial.h"
#include "runner_fake.h"

#define DEADLINE_MS ((tfw_ms_t)TFW_OTA_TRIAL_TIMEOUT_S * 1000u)

static void block(uint8_t boot_state, uint8_t *raw)
{
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    memset(raw, 0, TFW_BOOT_EE_LEN);
    p.boot_state = boot_state;
    tfw_boot_platform_serialise(&p, raw);
}

static void write_block(const uint8_t *raw)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN));
}

void setUp(void)    { tfw_mock_reset(); tfw_fake_runner_reset(); }
void tearDown(void) { }

/* Starts a trial, rewrites the block to `raw` half-way, runs past the deadline
 * and polls twice more. Returns the resets seen. */
static unsigned run_trial(const uint8_t *raw)
{
    uint8_t trial[TFW_BOOT_EE_LEN];

    setUp();
    block(TFW_BOOT_STATE_TRIAL, trial);
    write_block(trial);
    tfw_ota_trial_start();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_watchdog_arms());

    tfw_mock_advance(DEADLINE_MS / 2u);
    write_block(raw);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_watchdog_pets());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());

    tfw_mock_advance(DEADLINE_MS / 2u - 1u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(2u, tfw_mock_watchdog_pets());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());

    tfw_mock_advance(1u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(3u, tfw_mock_watchdog_pets());
    if (tfw_mock_reset_count() == 0u) {
        /* Disarmed: the next polls pet nothing and reset nothing. */
        tfw_mock_advance(DEADLINE_MS);
        tfw_ota_trial_poll();
        tfw_ota_trial_poll();
        TEST_ASSERT_EQUAL_UINT(3u, tfw_mock_watchdog_pets());
        TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    }
    return tfw_mock_reset_count();
}

static void test_every_boot_state_written_mid_trial(void)
{
    uint8_t  raw[TFW_BOOT_EE_LEN];
    unsigned state, resets = 0u;

    for (state = 0u; state <= 0xFFu; state++) {
        block((uint8_t)state, raw);
        if (run_trial(raw) != 0u) {
            TEST_ASSERT_EQUAL_UINT(TFW_BOOT_STATE_TRIAL, state);
            resets++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(1u, resets);
}

static void test_a_confirm_mid_trial_disarms_at_the_deadline(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];

    block(TFW_BOOT_STATE_CONFIRMED, raw);
    TEST_ASSERT_EQUAL_UINT(0u, run_trial(raw));
}

static void test_every_bit_flip_of_the_block_mid_trial(void)
{
    uint8_t  trial[TFW_BOOT_EE_LEN], raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;
    unsigned bit, runs = 0u;

    block(TFW_BOOT_STATE_TRIAL, trial);
    for (bit = 0u; bit < TFW_BOOT_EE_LEN * 8u; bit++) {
        unsigned want;

        memcpy(raw, trial, sizeof raw);
        raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        /* Resets only if the flipped block still parses as a trial. */
        want = (tfw_boot_platform_parse(raw, &p) == TFW_OK
                && p.boot_state == TFW_BOOT_STATE_TRIAL) ? 1u : 0u;
        TEST_ASSERT_EQUAL_UINT(want, run_trial(raw));
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_EE_LEN * 8u, runs);
}

static void test_an_erased_block_mid_trial_disarms(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];

    memset(raw, 0xFF, sizeof raw);
    TEST_ASSERT_EQUAL_UINT(0u, run_trial(raw));
    memset(raw, 0x00, sizeof raw);
    TEST_ASSERT_EQUAL_UINT(0u, run_trial(raw));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_boot_state_written_mid_trial);
    RUN_TEST(test_a_confirm_mid_trial_disarms_at_the_deadline);
    RUN_TEST(test_every_bit_flip_of_the_block_mid_trial);
    RUN_TEST(test_an_erased_block_mid_trial_disarms);
    return UNITY_END();
}
