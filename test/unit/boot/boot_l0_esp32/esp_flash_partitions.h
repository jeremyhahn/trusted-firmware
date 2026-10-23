/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Stand-in for ESP-IDF's esp_flash_partitions.h, for
 * test_boot_l0_esp32_mutate.c: ESP-IDF's layouts and OTA state values. */
#ifndef TFW_TEST_L0_ESP_FLASH_PARTITIONS_H
#define TFW_TEST_L0_ESP_FLASH_PARTITIONS_H

#include <stdint.h>

typedef struct {
    uint32_t offset;
    uint32_t size;
} esp_partition_pos_t;

typedef enum {
    ESP_OTA_IMG_NEW            = 0x0U,
    ESP_OTA_IMG_PENDING_VERIFY = 0x1U,
    ESP_OTA_IMG_VALID          = 0x2U,
    ESP_OTA_IMG_INVALID        = 0x3U,
    ESP_OTA_IMG_ABORTED        = 0x4U,
    ESP_OTA_IMG_UNDEFINED      = 0xFFFFFFFFU
} esp_ota_img_states_t;

typedef struct {
    uint32_t ota_seq;
    uint8_t  seq_label[20];
    uint32_t ota_state;
    uint32_t crc;
} esp_ota_select_entry_t;

#endif
