/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_efuse.h, for
 * test/unit/test_esp_hmac_p256_mutate.c alone: the key blocks, the key
 * purpose and the one call src/hal/esp32/crypto_hmac_p256.c makes, with the
 * ESP32-P4's and ESP32-C3's values. The test implements the call.
 */
#ifndef TFW_MOCK_ESP32_HMAC_ESP_EFUSE_H
#define TFW_MOCK_ESP32_HMAC_ESP_EFUSE_H

#include <stdbool.h>

typedef enum {
    EFUSE_BLK0     = 0,
    EFUSE_BLK1     = 1,
    EFUSE_BLK2     = 2,
    EFUSE_BLK3     = 3,
    EFUSE_BLK_KEY0 = 4,
    EFUSE_BLK_KEY1 = 5,
    EFUSE_BLK_KEY2 = 6,
    EFUSE_BLK_KEY3 = 7,
    EFUSE_BLK_KEY4 = 8,
    EFUSE_BLK_KEY5 = 9,
} esp_efuse_block_t;

typedef enum {
    ESP_EFUSE_KEY_PURPOSE_USER                         = 0,
    ESP_EFUSE_KEY_PURPOSE_XTS_AES_128_KEY              = 4,
    ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_ALL                = 5,
    ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_JTAG               = 6,
    ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE  = 7,
    ESP_EFUSE_KEY_PURPOSE_HMAC_UP                      = 8,
    ESP_EFUSE_KEY_PURPOSE_SECURE_BOOT_DIGEST0          = 9,
} esp_efuse_purpose_t;

bool esp_efuse_find_purpose(esp_efuse_purpose_t purpose, esp_efuse_block_t *block);

#endif
