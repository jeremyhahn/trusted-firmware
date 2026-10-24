/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_system.h: the reset-reason type
 * src/hal/esp32/hal_esp32.h names, with ESP-IDF's values in its order
 * (components/esp_system/include/esp_system.h), which hal_sys.c decodes.
 */
#ifndef TFW_MOCK_ESP_SYSTEM_H
#define TFW_MOCK_ESP_SYSTEM_H

typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON,
    ESP_RST_EXT,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO,
    ESP_RST_USB,
    ESP_RST_JTAG,
    ESP_RST_EFUSE,
    ESP_RST_PWR_GLITCH,
    ESP_RST_CPU_LOCKUP
} esp_reset_reason_t;

/* The two calls src/hal/esp32/hal_sys.c makes; test/mock/esp_sys_mock.c
 * keeps them. ESP-IDF declares esp_restart noreturn;
 * the stand-in does not, so the mock may record the call and return. */
esp_reset_reason_t esp_reset_reason(void);
void esp_restart(void);

#endif
