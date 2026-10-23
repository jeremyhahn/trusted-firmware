/*
 * Claim 273 through the real runner: tfw_attest_measurement(), which the
 * alias mint and ATTEST_CSR take claim 273 from, is the running application's
 * 32 bytes, and the firmware hash in every other case
 * (docs/application-sdk/engineering.md, Error semantics: FAILED and
 * UNSUPPORTED attest as if no application were linked). test_alias and
 * test_csr stand in for the runner and pin the bytes against
 * TFWV_APPEV_WITH_UCCS and TFWV_APPEV_WITHOUT_UCCS; test_app pins the runner
 * alone. This links the two together, beside those vectors, not in their
 * place.
 */
#include <string.h>
#include "unity.h"
#include "attest.h"
#include "sys.h"
#include "app_runner.h"
#include "app_mock.h"
#include "boot_soft_mock.h"
#include "hal_mock.h"
#include "tfw/app.h"
#include "vectors.h"

static const uint8_t UDS[32] = {
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA,
    0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5,
    0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF};
static const uint8_t FW[32] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
    0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
    0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F};
static const uint8_t EV[] = TFWV_APPEV_EXAMPLE;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_mock_app_reset();
    tfw_mock_boot_soft(UDS, FW);
    tfw_mock_app_set_evidence(TFW_OK, EV);
}

void tearDown(void) {}

static void assert_measurement_is(const uint8_t want[TFW_SHA256_LEN])
{
    uint8_t out[TFW_SHA256_LEN];

    memset(out, 0x5A, sizeof out);
    tfw_attest_measurement(out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, sizeof out);
}

static void test_the_firmware_hash_is_the_boot_measurement(void)
{
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FW, tfw_attest_fw_hash(), sizeof FW);
}

static void test_a_running_application_is_the_measurement(void)
{
    tfw_app_runner_start();
    assert_measurement_is(EV);
    TEST_ASSERT_EQUAL_STRING("ie", tfw_mock_app_log());
}

static void test_an_application_that_declines_leaves_the_firmware_hash(void)
{
    /* The mock writes its bytes into the buffer before it refuses. */
    tfw_mock_app_set_evidence(TFW_ERR_BAD_STATE, EV);
    tfw_app_runner_start();
    assert_measurement_is(FW);
}

static void test_an_application_not_yet_started_leaves_the_firmware_hash(void)
{
    assert_measurement_is(FW);
    TEST_ASSERT_EQUAL_STRING("", tfw_mock_app_log());
}

static void test_a_failed_application_leaves_the_firmware_hash(void)
{
    tfw_mock_app_set_init(TFW_ERR_BAD_STATE);
    tfw_app_runner_start();
    assert_measurement_is(FW);
    TEST_ASSERT_EQUAL_STRING("i", tfw_mock_app_log());
}

static void test_a_refused_application_leaves_the_firmware_hash(void)
{
    tfw_mock_app_descriptor()->magic = TFW_APP_MAGIC ^ 1u;
    tfw_app_runner_start();
    assert_measurement_is(FW);
    TEST_ASSERT_EQUAL_STRING("", tfw_mock_app_log());
}

static void test_a_stopped_application_leaves_the_firmware_hash(void)
{
    tfw_app_runner_start();
    tfw_app_runner_stop(0u);
    assert_measurement_is(FW);
    TEST_ASSERT_EQUAL_STRING("is", tfw_mock_app_log());
}

int main(void)
{
    UNITY_BEGIN();
    /* First: the runner's state lives until the next reset, and a process is
     * one reset, so IDLE is only seen before any test has started it. */
    RUN_TEST(test_an_application_not_yet_started_leaves_the_firmware_hash);
    RUN_TEST(test_the_firmware_hash_is_the_boot_measurement);
    RUN_TEST(test_a_running_application_is_the_measurement);
    RUN_TEST(test_an_application_that_declines_leaves_the_firmware_hash);
    RUN_TEST(test_a_failed_application_leaves_the_firmware_hash);
    RUN_TEST(test_a_refused_application_leaves_the_firmware_hash);
    RUN_TEST(test_a_stopped_application_leaves_the_firmware_hash);
    return UNITY_END();
}
