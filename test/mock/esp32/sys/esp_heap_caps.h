/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_heap_caps.h, for
 * test/unit/test_esp_sys_mutate.c alone: the one call and capability
 * src/hal/esp32/hal_sys.c names, with ESP-IDF's value. The test implements it.
 */
#ifndef TFW_MOCK_ESP32_SYS_ESP_HEAP_CAPS_H
#define TFW_MOCK_ESP32_SYS_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_INTERNAL (1u << 11)

size_t heap_caps_get_free_size(uint32_t caps);

#endif
