/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Stand-in for ESP-IDF's esp_rom_crc.h, for test_boot_l0_esp32_mutate.c. */
#ifndef TFW_TEST_L0_ESP_ROM_CRC_H
#define TFW_TEST_L0_ESP_ROM_CRC_H

#include <stdint.h>

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);

#endif
