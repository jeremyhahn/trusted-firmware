/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_err.h, for
 * test/unit/test_esp_prov_uds_mutate.c alone: the codes and the one call
 * src/hal/esp32/prov_uds.c makes, with ESP-IDF's values
 * (components/esp_common/include/esp_err.h). The test implements the call.
 */
#ifndef TFW_MOCK_ESP32_PROV_ESP_ERR_H
#define TFW_MOCK_ESP32_PROV_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK                0
#define ESP_FAIL              (-1)
#define ESP_ERR_INVALID_STATE 0x103

const char *esp_err_to_name(esp_err_t code);

#endif
