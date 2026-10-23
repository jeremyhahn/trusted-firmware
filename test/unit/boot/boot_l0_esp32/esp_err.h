/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Stand-in for ESP-IDF's esp_err.h, for test_boot_l0_esp32_mutate.c. */
#ifndef TFW_TEST_L0_ESP_ERR_H
#define TFW_TEST_L0_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK   0
#define ESP_FAIL (-1)

#endif
