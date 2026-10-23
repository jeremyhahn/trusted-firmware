/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's esp_flash_encrypt.h. The answer is an eFuse
 * read (FLASH_CRYPT_CNT); the test's stand-in returns the unfused value, false,
 * unless a test sets it.
 */
#ifndef TFW_STANDIN_ESP_FLASH_ENCRYPT_H
#define TFW_STANDIN_ESP_FLASH_ENCRYPT_H

#include <stdbool.h>

bool esp_flash_encryption_enabled(void);

#endif
