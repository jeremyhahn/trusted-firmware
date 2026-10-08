/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Reset cause, and issue #18: it must survive a bootloader that consumed
 * MCUSR before the application's own .init3 could -- tfw-boot, or a stock
 * Arduino bootloader, both do exactly that.
 *
 * tfw_reset_cause_decode is tested directly here because it is the ONE
 * decoder src/hal/avr/hal_sys.c and the mock both call -- see
 * src/core/reset_cause.h. tfw_hal_reset_cause's preference for a handed-off
 * cause is tested through the mock, which mirrors hal_sys.c's own logic.
 */

#include <string.h>
#include "unity.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "hal_mock.h"
#include "handoff.h"
#include "reset_cause.h"
#include "boot_measure.h"

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) {}

/* ------------------------------------------------------ the pure decoder */

static void test_each_bit_decodes_to_its_own_cause(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_POWER_ON, tfw_reset_cause_decode(TFW_MCUSR_PORF));
    TEST_ASSERT_EQUAL(TFW_RESET_EXTERNAL, tfw_reset_cause_decode(TFW_MCUSR_EXTRF));
    TEST_ASSERT_EQUAL(TFW_RESET_BROWNOUT, tfw_reset_cause_decode(TFW_MCUSR_BORF));
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_reset_cause_decode(TFW_MCUSR_WDRF));
}

static void test_no_bit_set_is_unknown(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_reset_cause_decode(0u));
}

/* Checked most specific first: a brownout during a watchdog reset sets both
 * bits, and the watchdog is the more actionable thing to report -- it names a
 * hang, where the brownout it also carries is often the hang's symptom, not
 * its cause. */
static void test_watchdog_wins_over_brownout(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG,
        tfw_reset_cause_decode(TFW_MCUSR_WDRF | TFW_MCUSR_BORF | TFW_MCUSR_PORF));
}

static void test_brownout_wins_over_external(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_BROWNOUT,
        tfw_reset_cause_decode(TFW_MCUSR_BORF | TFW_MCUSR_EXTRF));
}

/* ---------------------------------------------------- the handoff preference */

static void install_handoff(uint8_t reset_cause_byte)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32];

    memset(cdi, 0, sizeof cdi);
    memset(meas, 0, sizeof meas);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, meas, 1u,
                          reset_cause_byte);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
}

/* No bootloader ran, or one that predates the handoff, or a stock bootloader
 * -- all three leave the block exactly as tfw_mock_reset() zeroed it. This is
 * the case issue #18 says must fall back honestly rather than fabricate a
 * cause nobody observed. */
static void test_with_no_handoff_the_devices_own_observation_is_reported(void)
{
    tfw_mock_set_reset_cause(TFW_RESET_EXTERNAL);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_RESET_EXTERNAL, tfw_hal_reset_cause());
}

/*
 * THE HEADLINE CASE. A valid handoff carries the cause tfw-boot itself
 * observed, and it must be reported EVEN THOUGH the application's own
 * .init3 -- simulated here by tfw_mock_set_reset_cause -- saw something
 * completely different, which is exactly what happens for real: tfw-boot's
 * .init3 already cleared MCUSR by the time the application's ran.
 */
static void test_a_valid_handoff_overrides_the_devices_own_observation(void)
{
    install_handoff(TFW_MCUSR_WDRF);
    tfw_mock_set_reset_cause(TFW_RESET_POWER_ON);   /* what .init3 saw: nothing */
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_hal_reset_cause());
}

/* A block a stack grew into. Reported the same way a missing block is --
 * refused, not misread -- so the fallback is the device's own observation,
 * never a cause decoded from bytes that failed their CRC. */
static void test_a_corrupted_handoff_falls_back_rather_than_misreports(void)
{
    install_handoff(TFW_MCUSR_WDRF);
    tfw_mock_handoff()[TFW_BOOT_HO_RESET_CAUSE] ^= 0xFFu;   /* CRC now wrong */
    tfw_mock_set_reset_cause(TFW_RESET_BROWNOUT);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_RESET_BROWNOUT, tfw_hal_reset_cause());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_bit_decodes_to_its_own_cause);
    RUN_TEST(test_no_bit_set_is_unknown);
    RUN_TEST(test_watchdog_wins_over_brownout);
    RUN_TEST(test_brownout_wins_over_external);
    RUN_TEST(test_with_no_handoff_the_devices_own_observation_is_reported);
    RUN_TEST(test_a_valid_handoff_overrides_the_devices_own_observation);
    RUN_TEST(test_a_corrupted_handoff_falls_back_rather_than_misreports);
    return UNITY_END();
}
