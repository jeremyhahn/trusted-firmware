/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_timer.h, for
 * test/unit/test_esp_pulse_mutate.c alone: the microsecond count
 * src/hal/esp32/hal_pulse.c polls. The test implements it.
 */
#ifndef TFW_MOCK_ESP32_PULSE_ESP_TIMER_H
#define TFW_MOCK_ESP32_PULSE_ESP_TIMER_H

#include <stdint.h>

int64_t esp_timer_get_time(void);

#endif
