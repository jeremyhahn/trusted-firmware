/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_timer.h: the microsecond clock.
 * test/mock/esp_rmt_mock.c keeps it, advancing a fixed step per read.
 */
#ifndef TFW_MOCK_ESP_TIMER_H
#define TFW_MOCK_ESP_TIMER_H

#include <stdint.h>

int64_t esp_timer_get_time(void);

#endif
