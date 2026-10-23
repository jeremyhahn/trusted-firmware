/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_event.h, for
 * test/unit/test_esp_eth_mutate.c alone: the default loop and a handler
 * registration. The test implements both.
 */
#ifndef TFW_MOCK_ESP32_ETH_ESP_EVENT_H
#define TFW_MOCK_ESP32_ETH_ESP_EVENT_H

#include <stdint.h>
#include "esp_err.h"

#define ESP_ERR_INVALID_STATE 0x103

typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
                                     esp_event_handler_t handler, void *arg);

#endif
