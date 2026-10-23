/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Stand-in for ESP-IDF's esp_log.h, for test_boot_l0_esp32_mutate.c: every
 * line goes to the test's recorder, which keeps its level and its text. */
#ifndef TFW_TEST_L0_ESP_LOG_H
#define TFW_TEST_L0_ESP_LOG_H

#include <inttypes.h>

void l0_test_log(char level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define ESP_LOGE(...) l0_test_log('E', __VA_ARGS__)
#define ESP_LOGW(...) l0_test_log('W', __VA_ARGS__)
#define ESP_LOGI(...) l0_test_log('I', __VA_ARGS__)

#endif
