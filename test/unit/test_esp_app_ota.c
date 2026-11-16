/*
 * An application on an ESP32 image (#461, Codex round 3): the runner linked as
 * mk/arch/esp32.mk links it, with update_request_none.c (RESET(1) refused, no
 * tfw-boot session) and the architecture's own update path,
 * src/hal/esp32/hal_ota.c, over the esp_ota mock.
 *
 * The bootloader session is not the only update path: an application that
 * declares an OTA it relies on is served by ESP-IDF's two OTA slots, and must
 * start, not be refused before its init and then fail the trial that follows
 * the first update.
 */
#include "unity.h"
#include "tfw/config.h"
#include "tfw/app.h"
#include "esp_ota_mock.h"
#include "ota_stage.h"
#include "update_request.h"
#include "app_runner.h"
#include "app_mock.h"
#include "sys.h"

void setUp(void)
{
    mock_esp_reset();
    tfw_stage_init();
    tfw_sys_reset_state();
    tfw_mock_app_reset();
}
void tearDown(void) {}

static void starts_with_slots(uint8_t slots)
{
    tfw_mock_app_descriptor()->ota_slots = slots;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_STRING("i", tfw_mock_app_log());
}

/* The link this suite pins: no bootloader session on this image. */
static void test_the_image_has_no_bootloader_session(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_update_request_supported());
}

static void test_an_application_relying_on_one_slot_starts(void)
{
    starts_with_slots(TFW_APP_OTA_SINGLE);
}

/* ota_0 and ota_1: an A/B application is served too. */
static void test_an_application_relying_on_a_b_starts(void)
{
    starts_with_slots(TFW_APP_OTA_AB);
}

static void test_an_application_relying_on_no_update_starts(void)
{
    starts_with_slots(TFW_APP_OTA_NONE);
}

static void test_more_slots_than_esp_idf_has_are_refused(void)
{
    tfw_mock_app_descriptor()->ota_slots = (uint8_t)(TFW_APP_OTA_AB + 1u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_STRING("", tfw_mock_app_log());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_image_has_no_bootloader_session);
    RUN_TEST(test_an_application_relying_on_one_slot_starts);
    RUN_TEST(test_an_application_relying_on_a_b_starts);
    RUN_TEST(test_an_application_relying_on_no_update_starts);
    RUN_TEST(test_more_slots_than_esp_idf_has_are_refused);
    return UNITY_END();
}
