/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_err.h: the codes
 * src/hal/esp32/hal_ota.c and test/mock/esp_ota_mock.c name, with ESP-IDF's
 * values (components/esp_common/include/esp_err.h and esp_ota_ops.h).
 */
#ifndef TFW_MOCK_ESP_ERR_H
#define TFW_MOCK_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK                             0
#define ESP_FAIL                           (-1)
#define ESP_ERR_INVALID_ARG                0x102
#define ESP_ERR_INVALID_SIZE               0x104
#define ESP_ERR_NOT_FOUND                  0x105
#define ESP_ERR_OTA_BASE                   0x1500
#define ESP_ERR_OTA_PARTITION_CONFLICT     (ESP_ERR_OTA_BASE + 0x01)
#define ESP_ERR_OTA_VALIDATE_FAILED        (ESP_ERR_OTA_BASE + 0x03)
#define ESP_ERR_OTA_ROLLBACK_INVALID_STATE (ESP_ERR_OTA_BASE + 0x06)

#endif
