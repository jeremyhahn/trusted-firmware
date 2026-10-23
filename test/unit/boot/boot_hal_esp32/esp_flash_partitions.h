/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's esp_flash_partitions.h: the partition table
 * entry and where the table sits, with ESP-IDF's layout and values.
 */
#ifndef TFW_STANDIN_ESP_FLASH_PARTITIONS_H
#define TFW_STANDIN_ESP_FLASH_PARTITIONS_H

#include <stdint.h>

#define ESP_PARTITION_MAGIC        0x50AA
#define ESP_PARTITION_TABLE_OFFSET 0x8000
#define ESP_PARTITION_TABLE_MAX_LEN 0xC00

typedef struct {
    uint32_t offset;
    uint32_t size;
} esp_partition_pos_t;

typedef struct {
    uint16_t            magic;
    uint8_t             type;
    uint8_t             subtype;
    esp_partition_pos_t pos;
    uint8_t             label[16];
    uint32_t            flags;
} esp_partition_info_t;

#endif
