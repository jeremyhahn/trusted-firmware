/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_image_format.h (components/
 * bootloader_support): the records and the one call src/hal/esp32/hal_ota.c
 * makes. The structs keep ESP-IDF's names and the fields read here; the
 * behaviour is test/mock/esp_ota_mock.c's.
 */
#ifndef TFW_MOCK_ESP_IMAGE_FORMAT_H
#define TFW_MOCK_ESP_IMAGE_FORMAT_H

#include <stdint.h>
#include "esp_err.h"

#define ESP_ERR_IMAGE_BASE       0x2000
#define ESP_ERR_IMAGE_FLASH_FAIL (ESP_ERR_IMAGE_BASE + 1)
#define ESP_ERR_IMAGE_INVALID    (ESP_ERR_IMAGE_BASE + 2)

typedef struct {
    uint32_t offset;
    uint32_t size;
} esp_partition_pos_t;

typedef struct {
    uint8_t  magic;
    uint8_t  segment_count;
    uint8_t  spi_mode;
    uint8_t  spi_speed_size;
    uint32_t entry_addr;
    uint8_t  hash_appended;   /* ESP-IDF's last header byte; the rest elided */
} esp_image_header_t;

typedef struct {
    uint32_t           start_addr;
    esp_image_header_t image;
    uint32_t           image_len;   /* to the end of the appended SHA-256 */
    uint8_t            image_digest[32];
    uint32_t           secure_version;
} esp_image_metadata_t;

esp_err_t esp_image_get_metadata(const esp_partition_pos_t *part, esp_image_metadata_t *metadata);

#endif
