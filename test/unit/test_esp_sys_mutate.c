/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 system block, src/hal/esp32/hal_sys.c.
 *
 * The unit had no host build. This builds it over stand-ins for the ESP-IDF
 * calls it makes (test/mock/esp32/sys/), each implemented here and counted.
 * It holds:
 *
 *   - every esp_reset_reason_t decoded, and one past the last;
 *   - the reset cause a handoff carries winning over the chip's, but for
 *     SOFTWARE, which the chip's reason refines and only an unknown one keeps;
 *   - the restart each chip takes: the system reset on the ESP32
 *     (test_esp_sys_mutate), esp_restart() on the later chips
 *     (test_esp_sys_mutate_c3);
 *   - millis as microseconds / 1000, wrapped at 32 bits;
 *   - the delay, the free internal heap and board init passed through;
 *   - app_main dropping to the idle priority before it runs main.
 */

#include <stdint.h>
#include "unity.h"
#include "esp_heap_caps.h"
#include "esp_private/system_internal.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "tfw/hal.h"
#include "hal_esp32.h"
#include "../../src/core/handoff.h"

/* Neither is in a header: ESP-IDF calls the one, src/main.c declares the other. */
void         app_main(void);
tfw_status_t tfw_hal_tick_init(void);

static esp_reset_reason_t reason;
static uint8_t            handoff_present;
static uint8_t            handoff_cause;
static unsigned           restarts, noos_restarts;
static int64_t            now_us;
static uint32_t           delayed_us;
static unsigned           delays;
static uint32_t           heap_caps;
static size_t             heap_free;
static tfw_status_t       board_st;
static unsigned           board_inits;
static TaskHandle_t       prio_task;
static UBaseType_t        prio;
static unsigned           prio_sets, main_runs;
static int                in_suite;

esp_reset_reason_t esp_reset_reason(void)
{
    return reason;
}

void esp_restart(void)
{
    restarts++;
}

void esp_restart_noos_dig(void)
{
    noos_restarts++;
}

int64_t esp_timer_get_time(void)
{
    return now_us;
}

void esp_rom_delay_us(uint32_t us)
{
    delayed_us = us;
    delays++;
}

size_t heap_caps_get_free_size(uint32_t caps)
{
    heap_caps = caps;
    return heap_free;
}

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
    TEST_ASSERT_EQUAL_UINT(0u, main_runs);   /* before main, not after */
    prio_task = xTask;
    prio      = uxNewPriority;
    prio_sets++;
}

uint8_t tfw_boot_handoff_reset_cause(uint8_t *out)
{
    if (handoff_present) {
        *out = handoff_cause;
    }
    return handoff_present;
}

tfw_status_t tfw_esp_board_init(void)
{
    board_inits++;
    return board_st;
}

void setUp(void)
{
    reason          = ESP_RST_UNKNOWN;
    handoff_present = 0u;
    handoff_cause   = 0u;
    restarts        = 0u;
    noos_restarts   = 0u;
    now_us          = 0;
    delayed_us      = 0u;
    delays          = 0u;
    heap_caps       = 0u;
    heap_free       = 0u;
    board_st        = TFW_OK;
    board_inits     = 0u;
    prio_task       = (TaskHandle_t)&prio;
    prio            = 99u;
    prio_sets       = 0u;
    main_runs       = 0u;
}

void tearDown(void) {}

static const struct {
    esp_reset_reason_t esp;
    tfw_reset_cause_t  tfw;
} decode[] = {
    { ESP_RST_UNKNOWN,    TFW_RESET_UNKNOWN  },
    { ESP_RST_POWERON,    TFW_RESET_POWER_ON },
    { ESP_RST_EXT,        TFW_RESET_EXTERNAL },
    { ESP_RST_SW,         TFW_RESET_SOFTWARE },
    { ESP_RST_PANIC,      TFW_RESET_PANIC    },
    { ESP_RST_INT_WDT,    TFW_RESET_WATCHDOG },
    { ESP_RST_TASK_WDT,   TFW_RESET_WATCHDOG },
    { ESP_RST_WDT,        TFW_RESET_WATCHDOG },
    { ESP_RST_DEEPSLEEP,  TFW_RESET_UNKNOWN  },
    { ESP_RST_BROWNOUT,   TFW_RESET_BROWNOUT },
    { ESP_RST_SDIO,       TFW_RESET_UNKNOWN  },
    { ESP_RST_USB,        TFW_RESET_UNKNOWN  },
    { ESP_RST_JTAG,       TFW_RESET_UNKNOWN  },
    { ESP_RST_EFUSE,      TFW_RESET_UNKNOWN  },
    { ESP_RST_PWR_GLITCH, TFW_RESET_UNKNOWN  },
    { ESP_RST_CPU_LOCKUP, TFW_RESET_UNKNOWN  },
};
#define NDECODE (sizeof decode / sizeof decode[0])

static void test_every_reason_decodes(void)
{
    unsigned i;

    for (i = 0u; i < NDECODE; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(decode[i].tfw, tfw_esp_reset_decode(decode[i].esp),
                                      "esp_reset_reason_t by index");
    }
    TEST_ASSERT_EQUAL_INT(TFW_RESET_UNKNOWN,
                          tfw_esp_reset_decode((esp_reset_reason_t)(ESP_RST_CPU_LOCKUP + 1)));
}

static void test_with_no_handoff_the_chip_says(void)
{
    unsigned i;

    for (i = 0u; i < NDECODE; i++) {
        reason = decode[i].esp;
        TEST_ASSERT_EQUAL_INT(decode[i].tfw, tfw_hal_reset_cause());
    }
}

static void test_a_handoff_cause_wins_over_the_chip(void)
{
    static const uint8_t handed[] = {
        TFW_RESET_UNKNOWN, TFW_RESET_POWER_ON, TFW_RESET_EXTERNAL,
        TFW_RESET_BROWNOUT, TFW_RESET_WATCHDOG, TFW_RESET_PANIC,
    };
    unsigned i, j;

    handoff_present = 1u;
    for (i = 0u; i < sizeof handed; i++) {
        handoff_cause = handed[i];
        for (j = 0u; j < NDECODE; j++) {
            reason = decode[j].esp;
            TEST_ASSERT_EQUAL_INT(handed[i], tfw_hal_reset_cause());
        }
    }
}

static void test_a_handed_off_software_reset_is_refined_by_the_chip(void)
{
    unsigned i;

    handoff_present = 1u;
    handoff_cause   = TFW_RESET_SOFTWARE;
    for (i = 0u; i < NDECODE; i++) {
        tfw_reset_cause_t want = decode[i].tfw == TFW_RESET_UNKNOWN ? TFW_RESET_SOFTWARE
                                                                    : decode[i].tfw;
        reason = decode[i].esp;
        TEST_ASSERT_EQUAL_INT(want, tfw_hal_reset_cause());
    }
}

static void test_the_reset_is_the_one_this_chip_takes(void)
{
    tfw_hal_reset();
    TEST_ASSERT_EQUAL_UINT(CONFIG_IDF_TARGET_ESP32 ? 1u : 0u, noos_restarts);
    TEST_ASSERT_EQUAL_UINT(CONFIG_IDF_TARGET_ESP32 ? 0u : 1u, restarts);
}

static void test_millis_is_microseconds_over_1000_wrapped(void)
{
    static const struct {
        int64_t  us;
        tfw_ms_t ms;
    } t[] = {
        { 0, 0u },
        { 999, 0u },
        { 1000, 1u },
        { 1999999, 1999u },
        { 4294967295999LL, 4294967295u },
        { 4294967296000LL, 0u },
        { 4294967296000LL + 7999, 7u },
    };
    unsigned i;

    for (i = 0u; i < sizeof t / sizeof t[0]; i++) {
        now_us = t[i].us;
        TEST_ASSERT_EQUAL_UINT32(t[i].ms, tfw_hal_millis());
    }
}

static void test_the_delay_is_passed_through(void)
{
    static const uint16_t us[] = { 0u, 1u, 999u, 65535u };
    unsigned i;

    for (i = 0u; i < sizeof us / sizeof us[0]; i++) {
        tfw_hal_delay_us(us[i]);
        TEST_ASSERT_EQUAL_UINT32(us[i], delayed_us);
        TEST_ASSERT_EQUAL_UINT(i + 1u, delays);
    }
}

static void test_free_ram_is_the_internal_heap(void)
{
    heap_free = 123457u;
    TEST_ASSERT_EQUAL_UINT32(123457u, tfw_hal_free_ram());
    TEST_ASSERT_EQUAL_HEX32(MALLOC_CAP_INTERNAL, heap_caps);
}

static void test_init_is_the_boards(void)
{
    board_st = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_init());
    board_st = TFW_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_init());
    TEST_ASSERT_EQUAL_UINT(2u, board_inits);
}

static void test_the_tick_irq_and_watchdog_are_the_platforms(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_tick_init());
    tfw_hal_irq_enable();
    tfw_hal_watchdog_arm();
    tfw_hal_watchdog_pet();
    TEST_ASSERT_EQUAL_UINT(0u, restarts + noos_restarts + delays + board_inits);
}

static void test_app_main_drops_to_idle_then_runs_main(void)
{
    app_main();
    TEST_ASSERT_EQUAL_UINT(1u, prio_sets);
    TEST_ASSERT_NULL(prio_task);
    TEST_ASSERT_EQUAL_UINT(tskIDLE_PRIORITY, prio);
    TEST_ASSERT_EQUAL_UINT(1u, main_runs);
}

/* app_main runs main: the second entry is that, not the suite. */
int main(void)
{
    if (in_suite) {
        main_runs++;
        return 0;
    }
    in_suite = 1;
    UNITY_BEGIN();
    RUN_TEST(test_every_reason_decodes);
    RUN_TEST(test_with_no_handoff_the_chip_says);
    RUN_TEST(test_a_handoff_cause_wins_over_the_chip);
    RUN_TEST(test_a_handed_off_software_reset_is_refined_by_the_chip);
    RUN_TEST(test_the_reset_is_the_one_this_chip_takes);
    RUN_TEST(test_millis_is_microseconds_over_1000_wrapped);
    RUN_TEST(test_the_delay_is_passed_through);
    RUN_TEST(test_free_ram_is_the_internal_heap);
    RUN_TEST(test_init_is_the_boards);
    RUN_TEST(test_the_tick_irq_and_watchdog_are_the_platforms);
    RUN_TEST(test_app_main_drops_to_idle_then_runs_main);
    return UNITY_END();
}
