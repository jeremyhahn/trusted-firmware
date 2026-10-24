/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5's esp_heap_caps.h: the internal-memory
 * capability bit (BIT(11)) and the free-size query.
 * test/mock/esp_sys_mock.c keeps the query.
 */
#ifndef TFW_MOCK_ESP_HEAP_CAPS_H
#define TFW_MOCK_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_INTERNAL (1 << 11)

size_t heap_caps_get_free_size(uint32_t caps);

#endif
