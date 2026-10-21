/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_efuse.h: the reads
 * src/hal/esp32/hal_ota.c and crypto_hmac_p256.c make. There is no write here
 * to call. The block and purpose values are the P4's and the C3's
 * (components/efuse/esp32p4/include/esp_efuse_chip.h).
 */
#ifndef TFW_MOCK_ESP_EFUSE_H
#define TFW_MOCK_ESP_EFUSE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EFUSE_BLK_KEY0 = 4,
    EFUSE_BLK_KEY1 = 5,
    EFUSE_BLK_KEY2 = 6,
    EFUSE_BLK_KEY3 = 7,
    EFUSE_BLK_KEY4 = 8,
    EFUSE_BLK_KEY5 = 9,
    EFUSE_BLK_MAX  = 11
} esp_efuse_block_t;

typedef enum {
    ESP_EFUSE_KEY_PURPOSE_USER    = 0,
    ESP_EFUSE_KEY_PURPOSE_HMAC_UP = 8
} esp_efuse_purpose_t;

uint32_t esp_efuse_read_secure_version(void);
bool     esp_efuse_find_purpose(esp_efuse_purpose_t purpose, esp_efuse_block_t *block);

#endif
