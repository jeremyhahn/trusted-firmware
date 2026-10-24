/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP-IDF system calls src/hal/esp32/hal_sys.c and hal_handoff.c make,
 * on the host: each returns what test/mock/esp32/esp_sys_mock.h sets and
 * records that it was called. None waits and none resets.
 */
#include <string.h>

#include "esp_sys_mock.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "hal_esp32.h"
#include "../../src/core/handoff.h"

mock_sys_t mock_sys;

void mock_sys_reset(void)
{
    memset(&mock_sys, 0, sizeof mock_sys);
}

esp_reset_reason_t esp_reset_reason(void)
{
    mock_sys.reason_calls++;
    return mock_sys.reason;
}

void esp_restart(void)
{
    mock_sys.restart_calls++;
}

int64_t esp_timer_get_time(void)
{
    mock_sys.timer_calls++;
    return mock_sys.now_us;
}

void esp_rom_delay_us(uint32_t us)
{
    mock_sys.delay_calls++;
    mock_sys.delay_us = us;
}

size_t heap_caps_get_free_size(uint32_t caps)
{
    mock_sys.free_size_calls++;
    mock_sys.free_size_caps = caps;
    return mock_sys.free_size;
}

/* Only app_main calls it, and no suite runs app_main. */
void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
    (void)xTask;
    (void)uxNewPriority;
}

tfw_status_t tfw_esp_board_init(void)
{
    mock_sys.board_calls++;
    return mock_sys.board_status;
}

uint8_t tfw_boot_handoff_reset_cause(uint8_t *out)
{
    mock_sys.handoff_calls++;
    if (!mock_sys.handed_off) {
        return 0u;
    }
    *out = mock_sys.handoff_cause;
    return 1u;
}

rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void)
{
    mock_sys.retain_calls++;
    return &mock_sys.retain;
}
