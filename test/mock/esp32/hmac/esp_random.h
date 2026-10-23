/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_random.h, for
 * test/unit/test_esp_hmac_p256_mutate.c alone: the one call
 * src/hal/esp32/crypto_hmac_p256.c makes, which the test implements.
 */
#ifndef TFW_MOCK_ESP32_HMAC_ESP_RANDOM_H
#define TFW_MOCK_ESP32_HMAC_ESP_RANDOM_H

#include <stddef.h>

void esp_fill_random(void *buf, size_t len);

#endif
