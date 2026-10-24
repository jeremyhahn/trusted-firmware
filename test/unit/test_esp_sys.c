/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/esp32/hal_sys.c on the host, over the ESP-IDF system calls in
 * test/mock/esp_sys_mock.c: the reset cause (the bootloader's handoff first, a
 * handed-off software reset refined by esp_reset_reason, #48), the decode of
 * every esp_reset_reason_t, the millisecond clock and its wrap, and the calls
 * the reset, the delay, the free-RAM figure and the init make.
 *
 * Not here: app_main, which calls the firmware's main() -- on the host that is
 * this runner's own -- and the CONFIG_IDF_TARGET_ESP32 side of tfw_hal_reset
 * (esp_restart_noos_dig), which the stand-in sdkconfig.h does not select; one
 * host build reaches one side of it.
 */
#include "unity.h"
#include "esp_heap_caps.h"
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

static void test_with_no_handoff_the_cause_is_esp_idfs_reason_decoded(void)
{
    mock_sys.reason = ESP_RST_BROWNOUT;
    TEST_ASSERT_EQUAL(TFW_RESET_BROWNOUT, tfw_hal_reset_cause());
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.handoff_calls);
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.reason_calls);
}

static void test_a_handed_off_cause_other_than_software_is_returned_as_is(void)
{
    handed_off(TFW_RESET_EXTERNAL);
    mock_sys.reason = ESP_RST_PANIC;
    TEST_ASSERT_EQUAL(TFW_RESET_EXTERNAL, tfw_hal_reset_cause());
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, mock_sys.reason_calls,
                                   "esp_reset_reason was consulted over the handoff");
}

static void test_a_handed_off_software_reset_after_a_task_watchdog_is_a_watchdog(void)
{
    handed_off(TFW_RESET_SOFTWARE);
    mock_sys.reason = ESP_RST_TASK_WDT;
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_hal_reset_cause());
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.reason_calls);
}

static void test_a_handed_off_software_reset_after_a_panic_is_a_panic(void)
{
    handed_off(TFW_RESET_SOFTWARE);
    mock_sys.reason = ESP_RST_PANIC;
    TEST_ASSERT_EQUAL(TFW_RESET_PANIC, tfw_hal_reset_cause());
}

static void test_a_handed_off_software_reset_after_a_requested_restart_is_software(void)
{
    handed_off(TFW_RESET_SOFTWARE);
    mock_sys.reason = ESP_RST_SW;
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_hal_reset_cause());
}

static void test_a_handed_off_software_reset_with_an_unknown_reason_stays_software(void)
{
    handed_off(TFW_RESET_SOFTWARE);
    mock_sys.reason = ESP_RST_DEEPSLEEP;
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_hal_reset_cause());
    mock_sys.reason = ESP_RST_UNKNOWN;
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_hal_reset_cause());
}

/* tfw_esp_reset_decode */

static void test_every_reason_with_a_cause_of_its_own_decodes_to_it(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_POWER_ON, tfw_esp_reset_decode(ESP_RST_POWERON));
    TEST_ASSERT_EQUAL(TFW_RESET_EXTERNAL, tfw_esp_reset_decode(ESP_RST_EXT));
    TEST_ASSERT_EQUAL(TFW_RESET_BROWNOUT, tfw_esp_reset_decode(ESP_RST_BROWNOUT));
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_esp_reset_decode(ESP_RST_SW));
    TEST_ASSERT_EQUAL(TFW_RESET_PANIC, tfw_esp_reset_decode(ESP_RST_PANIC));
}

static void test_every_watchdog_reason_decodes_to_watchdog(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_esp_reset_decode(ESP_RST_INT_WDT));
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_esp_reset_decode(ESP_RST_TASK_WDT));
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_esp_reset_decode(ESP_RST_WDT));
}

static void test_a_reason_with_no_cause_of_its_own_decodes_to_unknown(void)
{
    static const esp_reset_reason_t none[] = {
        ESP_RST_UNKNOWN, ESP_RST_DEEPSLEEP, ESP_RST_SDIO, ESP_RST_USB,
        ESP_RST_JTAG, ESP_RST_EFUSE, ESP_RST_PWR_GLITCH, ESP_RST_CPU_LOCKUP,
    };
    for (unsigned i = 0u; i < sizeof none / sizeof none[0]; i++) {
        TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, tfw_esp_reset_decode(none[i]));
    }
}

/* tfw_hal_millis */

static void test_millis_is_the_microsecond_clock_divided_down(void)
{
    mock_sys.now_us = 999;
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_hal_millis());
    mock_sys.now_us = 1000;
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_hal_millis());
    mock_sys.now_us = 123456789;
    TEST_ASSERT_EQUAL_UINT32(123456u, tfw_hal_millis());
}

static void test_millis_wraps_at_two_to_the_thirty_second(void)
{
    mock_sys.now_us = ((int64_t)UINT32_MAX) * 1000;
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tfw_hal_millis());
    mock_sys.now_us = (((int64_t)1 << 32) + 5) * 1000;
    TEST_ASSERT_EQUAL_UINT32(5u, tfw_hal_millis());
}

/* tfw_hal_reset, tfw_hal_delay_us, tfw_hal_free_ram */

static void test_reset_restarts_through_esp_idf(void)
{
    tfw_hal_reset();
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.restart_calls);
}

static void test_the_delay_is_the_roms_busy_wait_for_the_same_microseconds(void)
{
    tfw_hal_delay_us(65535u);
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.delay_calls);
    TEST_ASSERT_EQUAL_UINT32(65535u, mock_sys.delay_us);
}

static void test_free_ram_is_the_free_internal_heap(void)
{
    mock_sys.free_size = 201234u;
    TEST_ASSERT_EQUAL_UINT32(201234u, tfw_hal_free_ram());
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.free_size_calls);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)MALLOC_CAP_INTERNAL, mock_sys.free_size_caps);
}

/* tfw_hal_init, tfw_hal_tick_init and the no-ops */

static void test_init_runs_the_board_bring_up_once(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_init());
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.board_calls);
}

static void test_init_reports_the_board_bring_ups_refusal(void)
{
    mock_sys.board_status = TFW_ERR_INTERNAL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_init());
}

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
    RUN_TEST(test_with_no_handoff_the_cause_is_esp_idfs_reason_decoded);
    RUN_TEST(test_a_handed_off_cause_other_than_software_is_returned_as_is);
    RUN_TEST(test_a_handed_off_software_reset_after_a_task_watchdog_is_a_watchdog);
    RUN_TEST(test_a_handed_off_software_reset_after_a_panic_is_a_panic);
    RUN_TEST(test_a_handed_off_software_reset_after_a_requested_restart_is_software);
    RUN_TEST(test_a_handed_off_software_reset_with_an_unknown_reason_stays_software);
    RUN_TEST(test_every_reason_with_a_cause_of_its_own_decodes_to_it);
    RUN_TEST(test_every_watchdog_reason_decodes_to_watchdog);
    RUN_TEST(test_a_reason_with_no_cause_of_its_own_decodes_to_unknown);
    RUN_TEST(test_millis_is_the_microsecond_clock_divided_down);
    RUN_TEST(test_millis_wraps_at_two_to_the_thirty_second);
    RUN_TEST(test_reset_restarts_through_esp_idf);
    RUN_TEST(test_the_delay_is_the_roms_busy_wait_for_the_same_microseconds);
    RUN_TEST(test_free_ram_is_the_free_internal_heap);
    RUN_TEST(test_init_runs_the_board_bring_up_once);
    RUN_TEST(test_init_reports_the_board_bring_ups_refusal);
    RUN_TEST(test_the_tick_is_esp_timers_and_needs_no_start);
    RUN_TEST(test_irq_enable_and_the_watchdog_touch_nothing);
    return UNITY_END();
}
