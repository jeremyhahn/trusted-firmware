/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_timer.h, for
 * test/unit/test_esp_sys_mutate.c alone: microseconds since boot, as a
 * signed 64-bit count. The test implements it.
 */
#ifndef TFW_MOCK_ESP32_SYS_ESP_TIMER_H
#define TFW_MOCK_ESP32_SYS_ESP_TIMER_H

#include <stdint.h>

int64_t esp_timer_get_time(void);

#endif
