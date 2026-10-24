/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The controls of test/mock/esp_sys_mock.c: what the ESP-IDF system calls
 * src/hal/esp32/hal_sys.c and hal_handoff.c make return, and a record of each
 * call -- how many, and the last argument.
 *
 * The handoff seam is src/core/handoff.c's tfw_boot_handoff_reset_cause,
 * stubbed: `handed_off` says whether there was a handoff and `handoff_cause`
 * is the cause it carried.
 */
#ifndef TFW_MOCK_ESP_SYS_MOCK_H
#define TFW_MOCK_ESP_SYS_MOCK_H

#include <stddef.h>
#include <stdint.h>
#include "bootloader_common.h"
#include "esp_system.h"
#include "tfw/status.h"

typedef struct {
    esp_reset_reason_t reason;          /* esp_reset_reason's */
    int64_t            now_us;          /* esp_timer_get_time's */
    size_t             free_size;       /* heap_caps_get_free_size's */
    tfw_status_t       board_status;    /* tfw_esp_board_init's */
    uint8_t            handed_off;      /* tfw_boot_handoff_reset_cause's */
    uint8_t            handoff_cause;
    rtc_retain_mem_t   retain;          /* bootloader_common_get_rtc_retain_mem's */

    unsigned reason_calls, restart_calls, delay_calls, free_size_calls,
             board_calls, handoff_calls, retain_calls, timer_calls;
    uint32_t delay_us;                  /* the last esp_rom_delay_us argument */
    uint32_t free_size_caps;            /* the last heap_caps_get_free_size argument */
} mock_sys_t;

extern mock_sys_t mock_sys;

/* Every return zero (ESP_RST_UNKNOWN, TFW_OK, no handoff), nothing called. */
void mock_sys_reset(void);

#endif
