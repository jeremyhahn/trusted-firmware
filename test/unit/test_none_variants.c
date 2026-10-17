/*
 * The no-boot stand-ins (#130): what a build without tfw-boot links in place
 * of the handoff, the platform block, the stager and PREV. What they answer is
 * what a no-boot board reports to a host, so each answer is pinned here.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "handoff.h"
#include "platform_id.h"
#include "ota_stage.h"

void setUp(void) {}
void tearDown(void) {}

static void test_handoff_measurement_is_null(void)
{
    tfw_boot_handoff_init();
    TEST_ASSERT_NULL(tfw_boot_handoff_measurement());
}

static void test_handoff_fw_hash_is_all_zero_for_its_length(void)
{
    const uint8_t *hash = tfw_boot_handoff_fw_hash();
    TEST_ASSERT_NOT_NULL(hash);
    TEST_ASSERT_EACH_EQUAL_UINT8(0u, hash, TFW_BOOT_MEASURE_LEN);
}

static void test_handoff_reset_cause_returns_zero_and_leaves_out_untouched(void)
{
    uint8_t cause = 0xA5u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_reset_cause(&cause));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, cause);
}

static void test_handoff_tier_is_null(void)
{
    TEST_ASSERT_NULL(tfw_boot_handoff_tier());
}

static void test_handoff_take_cdi_returns_zero_and_leaves_out_untouched(void)
{
    uint8_t cdi[TFW_BOOT_MEASURE_LEN];
    memset(cdi, 0xA5, sizeof cdi);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_take_cdi(cdi));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xA5u, cdi, sizeof cdi);
}

static void test_platform_serial_is_all_zero(void)
{
    const uint8_t *serial;
    tfw_platform_id_init();
    serial = tfw_platform_id_serial();
    TEST_ASSERT_NOT_NULL(serial);
    TEST_ASSERT_EACH_EQUAL_UINT8(0u, serial, TFW_BOOT_SERIAL_LEN);
}

static void test_platform_operator_pubkey_is_all_zero_and_no_wider(void)
{
    uint8_t key[TFW_BOOT_OPERATOR_PUBKEY_LEN + 1u];
    memset(key, 0xA5, sizeof key);
    tfw_platform_id_operator_pubkey(key);
    TEST_ASSERT_EACH_EQUAL_UINT8(0u, key, TFW_BOOT_OPERATOR_PUBKEY_LEN);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, key[TFW_BOOT_OPERATOR_PUBKEY_LEN]);
}

static void test_stage_init_is_safe_to_call_twice(void)
{
    tfw_stage_init();
    tfw_stage_init();
    TEST_PASS();
}

static void test_ota_prev_backup_answers_ok(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_prev_backup());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_handoff_measurement_is_null);
    RUN_TEST(test_handoff_fw_hash_is_all_zero_for_its_length);
    RUN_TEST(test_handoff_reset_cause_returns_zero_and_leaves_out_untouched);
    RUN_TEST(test_handoff_tier_is_null);
    RUN_TEST(test_handoff_take_cdi_returns_zero_and_leaves_out_untouched);
    RUN_TEST(test_platform_serial_is_all_zero);
    RUN_TEST(test_platform_operator_pubkey_is_all_zero_and_no_wider);
    RUN_TEST(test_stage_init_is_safe_to_call_twice);
    RUN_TEST(test_ota_prev_backup_answers_ok);
    return UNITY_END();
}
