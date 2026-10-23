/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for ESP-IDF's esp_efuse.h, for test_boot_l0_esp32_mutate.c: the
 * two reads boot_l0.c makes, with ESP-IDF's enumerator values. Nothing here
 * writes or burns an eFuse; the test answers each read with a value it chose.
 */
#ifndef TFW_TEST_L0_ESP_EFUSE_H
#define TFW_TEST_L0_ESP_EFUSE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EFUSE_BLK0     = 0,
    EFUSE_BLK_KEY0 = 4,
    EFUSE_BLK_KEY1 = 5,
    EFUSE_BLK_KEY2 = 6,
    EFUSE_BLK_KEY3 = 7,
    EFUSE_BLK_KEY4 = 8,
    EFUSE_BLK_KEY5 = 9
} esp_efuse_block_t;

typedef enum {
    ESP_EFUSE_KEY_PURPOSE_USER                 = 0,
    ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_ALL        = 5,
    ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_JTAG       = 6,
    ESP_EFUSE_KEY_PURPOSE_HMAC_DOWN_DIGITAL_SIGNATURE = 7,
    ESP_EFUSE_KEY_PURPOSE_HMAC_UP              = 8
} esp_efuse_purpose_t;

bool     esp_efuse_find_purpose(esp_efuse_purpose_t purpose, esp_efuse_block_t *block);
uint32_t esp_efuse_read_secure_version(void);

#endif
