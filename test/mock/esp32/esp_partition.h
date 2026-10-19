/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_partition.h: the partition
 * record, laid out as ESP-IDF's, and the two calls src/hal/esp32/hal_ota.c makes.
 */
#ifndef TFW_MOCK_ESP_PARTITION_H
#define TFW_MOCK_ESP_PARTITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    void    *flash_chip;
    int      type;
    int      subtype;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    char     label[17];
    bool     encrypted;
    bool     readonly;
} esp_partition_t;

esp_err_t esp_partition_get_sha256(const esp_partition_t *partition, uint8_t *sha_256);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size);

#endif
