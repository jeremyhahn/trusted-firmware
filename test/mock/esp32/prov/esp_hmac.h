/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_hmac.h, for
 * test/unit/test_esp_prov_uds_mutate.c alone: the key ids and the one call
 * src/hal/esp32/prov_uds.c makes. The test implements the call.
 */
#ifndef TFW_MOCK_ESP32_PROV_ESP_HMAC_H
#define TFW_MOCK_ESP32_PROV_ESP_HMAC_H

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

esp_err_t esp_hmac_calculate(hmac_key_id_t key_id, const void *message, size_t message_len,
                             uint8_t *hmac);

#endif
