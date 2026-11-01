/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/esp32/hal_sys.c on the host, over the ESP-IDF system calls in
 * test/mock/esp_sys_mock.c. The decode, the reset cause, the clock, the delay,
 * the free-RAM figure and the init are pinned by test_esp_sys_mutate.c; what
 * stays here is what that does not assert: a handed-off cause other than
 * software never consults esp_reset_reason, the reset on this build's side of
 * CONFIG_IDF_TARGET_ESP32, and that the tick and the no-ops start no timer.
 *
 * Not here: app_main, which calls the firmware's main() -- on the host that is
 * this runner's own -- and the CONFIG_IDF_TARGET_ESP32 side of tfw_hal_reset
 * (esp_restart_noos_dig), which the stand-in sdkconfig.h does not select; one
 * host build reaches one side of it.
 */
#include "unity.h"
#include "esp_sys_mock.h"
#include "hal_esp32.h"
#include "tfw/hal.h"

/* No header declares it; src/main.c names it the same way. */
tfw_status_t tfw_hal_tick_init(void);

void setUp(void)
{
    mock_sys_reset();
}

void tearDown(void) {}

static void handed_off(tfw_reset_cause_t cause)
{
    mock_sys.handed_off    = 1u;
    mock_sys.handoff_cause = (uint8_t)cause;
}

/* tfw_hal_reset_cause */

static void test_a_handed_off_cause_other_than_software_is_returned_as_is(void)
{
    handed_off(TFW_RESET_EXTERNAL);
    mock_sys.reason = ESP_RST_PANIC;
    TEST_ASSERT_EQUAL(TFW_RESET_EXTERNAL, tfw_hal_reset_cause());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, mock_sys.reason_calls,
                                   "esp_reset_reason was consulted over the handoff");
}

/* tfw_hal_reset */

static void test_reset_restarts_through_esp_idf(void)
{
    tfw_hal_reset();
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.restart_calls);
}

/* tfw_hal_tick_init and the no-ops */

static void test_the_tick_is_esp_timers_and_needs_no_start(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tick_init());
    TEST_ASSERT_EQUAL_UINT(0u, mock_sys.timer_calls);
}

static void test_irq_enable_and_the_watchdog_touch_nothing(void)
{
    tfw_hal_irq_enable();
    tfw_hal_watchdog_arm();
    tfw_hal_watchdog_pet();
    TEST_ASSERT_EQUAL_UINT(0u, mock_sys.restart_calls + mock_sys.board_calls
                               + mock_sys.timer_calls + mock_sys.delay_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_handed_off_cause_other_than_software_is_returned_as_is);
    RUN_TEST(test_reset_restarts_through_esp_idf);
    RUN_TEST(test_the_tick_is_esp_timers_and_needs_no_start);
    RUN_TEST(test_irq_enable_and_the_watchdog_touch_nothing);
    return UNITY_END();
}
