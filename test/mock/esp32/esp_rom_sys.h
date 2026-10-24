/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5's esp_rom_sys.h: the ROM's busy-wait.
 * test/mock/esp_sys_mock.c records it and does not wait.
 */
#ifndef TFW_MOCK_ESP_ROM_SYS_H
#define TFW_MOCK_ESP_ROM_SYS_H

#include <stdint.h>

void esp_rom_delay_us(uint32_t us);

#endif
