/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_partition.h, as far as
 * src/hal/esp32/hal_eeprom.c needs it: the partition record, laid out as
 * ESP-IDF's, the lookup by label, and read, erase and write. It sits in its
 * own include directory so the shared stand-in beside it keeps the two
 * calls hal_ota.c makes and no more. The test that includes it defines the
 * calls.
 */
#ifndef TFW_MOCK_ESP_PARTITION_EEPROM_H
#define TFW_MOCK_ESP_PARTITION_EEPROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    ESP_PARTITION_TYPE_APP  = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
} esp_partition_type_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

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

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size);
esp_err_t esp_partition_write(const esp_partition_t *partition, size_t dst_offset, const void *src, size_t size);

#endif
