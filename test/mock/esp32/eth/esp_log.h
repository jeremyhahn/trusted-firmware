/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_log.h, for
 * test/unit/test_esp_eth_mutate.c alone: the info level
 * src/hal/esp32/hal_eth.c logs at, checked as a printf format the way
 * ESP-IDF's own esp_log_write is. The test implements mock_esp_log.
 */
#ifndef TFW_MOCK_ESP32_ETH_ESP_LOG_H
#define TFW_MOCK_ESP32_ETH_ESP_LOG_H

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define ESP_LOGI(tag, ...) mock_esp_log('I', (tag), __VA_ARGS__)

#endif
