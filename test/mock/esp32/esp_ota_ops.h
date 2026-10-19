/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_ota_ops.h (components/app_update),
 * with esp_app_desc_t's leading fields from esp_app_desc.h. Values and
 * signatures are ESP-IDF's; the behaviour behind them is test/mock/esp_ota_mock.c.
 */
#ifndef TFW_MOCK_ESP_OTA_OPS_H
#define TFW_MOCK_ESP_OTA_OPS_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_partition.h"

#define OTA_SIZE_UNKNOWN           0xffffffffu
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffeu

typedef uint32_t esp_ota_handle_t;

typedef enum {
    ESP_OTA_IMG_NEW            = 0x0u,
    ESP_OTA_IMG_PENDING_VERIFY = 0x1u,
    ESP_OTA_IMG_VALID          = 0x2u,
    ESP_OTA_IMG_INVALID        = 0x3u,
    ESP_OTA_IMG_ABORTED        = 0x4u,
    ESP_OTA_IMG_UNDEFINED      = 0xFFFFFFFFu
} esp_ota_img_states_t;

/* At image offset 0x20, after the 24-byte image header and the first 8-byte
 * segment header. ESP-IDF's struct continues past project_name; nothing here
 * reads further. */
#define ESP_APP_DESC_MAGIC_WORD 0xABCD5432u
typedef struct {
    uint32_t magic_word;
    uint32_t secure_version;
    uint32_t reserv1[2];
    char     version[32];
    char     project_name[32];
} esp_app_desc_t;

const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_boot_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size,
                        esp_ota_handle_t *out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition,
                                      esp_ota_img_states_t *ota_state);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                            esp_app_desc_t *app_desc);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);

#endif
