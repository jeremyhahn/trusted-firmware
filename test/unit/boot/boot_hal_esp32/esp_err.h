/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's esp_err.h: the two codes boot_hal_esp32.c
 * names, with ESP-IDF's values.
 */
#ifndef TFW_STANDIN_ESP_ERR_H
#define TFW_STANDIN_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK   0
#define ESP_FAIL (-1)

#endif
