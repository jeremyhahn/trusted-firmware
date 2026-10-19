/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_log.h: the one level
 * src/hal/esp32/hal_uart.c logs at, recorded by test/mock/lwip_mock.c so a
 * suite can assert that a path said why, and checked as a printf format the way
 * ESP-IDF's own esp_log_write is.
 */
#ifndef TFW_MOCK_ESP_LOG_H
#define TFW_MOCK_ESP_LOG_H

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define ESP_LOGW(tag, ...) mock_esp_log('W', (tag), __VA_ARGS__)

#endif
