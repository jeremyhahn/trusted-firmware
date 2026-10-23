/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_rom_sys.h, for
 * test/unit/test_esp_sys_mutate.c alone. The test implements the call.
 */
#ifndef TFW_MOCK_ESP32_SYS_ESP_ROM_SYS_H
#define TFW_MOCK_ESP32_SYS_ESP_ROM_SYS_H

#include <stdint.h>

void esp_rom_delay_us(uint32_t us);

#endif
