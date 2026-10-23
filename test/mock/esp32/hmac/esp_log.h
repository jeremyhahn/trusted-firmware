/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_log.h, for
 * test/unit/test_esp_hmac_p256_mutate.c alone: the three levels
 * src/hal/esp32/crypto_hmac_p256.c logs at, as one call the test implements,
 * checked as a printf format the way ESP-IDF's own esp_log_write is.
 */
#ifndef TFW_MOCK_ESP32_HMAC_ESP_LOG_H
#define TFW_MOCK_ESP32_HMAC_ESP_LOG_H

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define ESP_LOGE(tag, ...) mock_esp_log('E', (tag), __VA_ARGS__)
#define ESP_LOGW(tag, ...) mock_esp_log('W', (tag), __VA_ARGS__)
#define ESP_LOGI(tag, ...) mock_esp_log('I', (tag), __VA_ARGS__)

#endif
