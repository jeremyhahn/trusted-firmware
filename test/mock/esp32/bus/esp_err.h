/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_err.h, for
 * test/unit/test_esp_bus_mutate.c alone: the codes src/hal/esp32/hal_bus.c
 * and its test use, with ESP-IDF's values.
 */
#ifndef TFW_MOCK_ESP32_BUS_ESP_ERR_H
#define TFW_MOCK_ESP32_BUS_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK              0
#define ESP_FAIL            (-1)
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_FOUND   0x105
#define ESP_ERR_TIMEOUT     0x107

#endif
