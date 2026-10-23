/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for ESP-IDF's esp_image_format.h, for test_boot_l0_esp32_mutate.c.
 * The image header is ESP-IDF's 24 bytes and the segment header its 8, so
 * boot_l0.c's digest_end does the device's arithmetic.
 */
#ifndef TFW_TEST_L0_ESP_IMAGE_FORMAT_H
#define TFW_TEST_L0_ESP_IMAGE_FORMAT_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_flash_partitions.h"

#define ESP_IMAGE_MAX_SEGMENTS 16

typedef enum {
    ESP_IMAGE_VERIFY        = 0,
    ESP_IMAGE_VERIFY_SILENT = 1,
    ESP_IMAGE_LOAD          = 2
} esp_image_load_mode_t;

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  segment_count;
    uint8_t  spi_mode;
    uint8_t  spi_speed_size;
    uint32_t entry_addr;
    uint8_t  wp_pin;
    uint8_t  spi_pin_drv[3];
    uint16_t chip_id;
    uint8_t  min_chip_rev;
    uint16_t min_chip_rev_full;
    uint16_t max_chip_rev_full;
    uint8_t  reserved[4];
    uint8_t  hash_appended;
} esp_image_header_t;

typedef struct {
    uint32_t load_addr;
    uint32_t data_len;
} esp_image_segment_header_t;

typedef struct {
    uint32_t                   start_addr;
    esp_image_header_t         image;
    esp_image_segment_header_t segments[ESP_IMAGE_MAX_SEGMENTS];
    uint32_t                   segment_data[ESP_IMAGE_MAX_SEGMENTS];
    uint32_t                   image_len;
    uint8_t                    image_digest[32];
    uint32_t                   secure_version;
} esp_image_metadata_t;

esp_err_t esp_image_verify(esp_image_load_mode_t mode, const esp_partition_pos_t *part,
                           esp_image_metadata_t *data);

#endif
