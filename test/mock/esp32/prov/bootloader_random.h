/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's bootloader_random.h, for
 * test/unit/test_esp_prov_uds_mutate.c alone: the entropy source's switch,
 * which the test implements.
 */
#ifndef TFW_MOCK_ESP32_PROV_BOOTLOADER_RANDOM_H
#define TFW_MOCK_ESP32_PROV_BOOTLOADER_RANDOM_H

void bootloader_random_enable(void);
void bootloader_random_disable(void);

#endif
