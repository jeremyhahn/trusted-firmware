/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_hmac.h: the one call
 * src/hal/esp32/crypto_hmac_p256.c makes, the HMAC peripheral's MAC under an
 * eFuse key block it never reads. The suite defines esp_hmac_calculate over a
 * key it holds, as the peripheral holds the UDS.
 */
#ifndef TFW_MOCK_ESP_HMAC_H
#define TFW_MOCK_ESP_HMAC_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    HMAC_KEY0 = 0,
    HMAC_KEY1,
    HMAC_KEY2,
    HMAC_KEY3,
    HMAC_KEY4,
    HMAC_KEY5,
    HMAC_KEY_MAX
} hmac_key_id_t;

esp_err_t esp_hmac_calculate(hmac_key_id_t key_id, const void *message,
                             size_t message_len, uint8_t *hmac);

#endif
