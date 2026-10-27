/* The application side of the update request (src/core/update_request.c):
 * arm() encodes a boot request into tfw_hal_handoff_block(), and the
 * bootloader's decoder (boot/boot_request.c) reads it back. The decoder only
 * honours a request after a watchdog reset (boot/boot_request.c:16). */

#include <stdint.h>
#include "unity.h"
#include "tfw/types.h"
#include "tfw/hal.h"
#include "hal_mock.h"
#include "update_request.h"
#include "boot_request.h"

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) {}

static void test_update_request_is_supported(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_update_request_supported());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_update_request_session());
}

static void test_handoff_block_is_not_a_boot_request_before_arm(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(tfw_hal_handoff_block(), TFW_RESET_WATCHDOG));
}

static void test_handoff_block_decodes_as_a_boot_request_after_arm(void)
{
    tfw_update_request_arm();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_request_valid(tfw_hal_handoff_block(), TFW_RESET_WATCHDOG));
}

static void test_armed_request_is_ignored_after_a_non_watchdog_reset(void)
{
    tfw_update_request_arm();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(tfw_hal_handoff_block(), TFW_RESET_POWER_ON));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_update_request_is_supported);
    RUN_TEST(test_handoff_block_is_not_a_boot_request_before_arm);
    RUN_TEST(test_handoff_block_decodes_as_a_boot_request_after_arm);
    RUN_TEST(test_armed_request_is_ignored_after_a_non_watchdog_reset);
    return UNITY_END();
}
