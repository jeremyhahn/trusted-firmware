/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_random.h: the part's RNG, which
 * src/hal/esp32/crypto_hmac_p256.c reads for tfw_crypto_random and micro-ecc's
 * blinding. The suite defines it.
 */
#ifndef TFW_MOCK_ESP_RANDOM_H
#define TFW_MOCK_ESP_RANDOM_H

#include <stddef.h>

void esp_fill_random(void *buf, size_t len);

#endif
