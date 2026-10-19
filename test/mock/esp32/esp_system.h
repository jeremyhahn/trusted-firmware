/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_system.h: the one type
 * src/hal/esp32/hal_esp32.h names, with ESP-IDF's first value
 * (components/esp_system/include/esp_system.h). Nothing on the host reads a
 * reset reason; this is here so the HAL's own header compiles.
 */
#ifndef TFW_MOCK_ESP_SYSTEM_H
#define TFW_MOCK_ESP_SYSTEM_H

typedef enum {
    ESP_RST_UNKNOWN = 0
} esp_reset_reason_t;

#endif
