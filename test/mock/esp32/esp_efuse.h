/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_efuse.h: the one read
 * src/hal/esp32/hal_ota.c makes. There is no write here to call.
 */
#ifndef TFW_MOCK_ESP_EFUSE_H
#define TFW_MOCK_ESP_EFUSE_H

#include <stdint.h>

uint32_t esp_efuse_read_secure_version(void);

#endif
