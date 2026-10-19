/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's bootloader_sha.h: the SHA-256
 * boot/hal/esp32/measure_esp32.c hashes through. The definition is
 * test/mock/esp_sha_mock.c.
 */
#ifndef TFW_MOCK_BOOTLOADER_SHA_H
#define TFW_MOCK_BOOTLOADER_SHA_H

#include <stddef.h>
#include <stdint.h>

typedef void *bootloader_sha256_handle_t;

bootloader_sha256_handle_t bootloader_sha256_start(void);
void bootloader_sha256_data(bootloader_sha256_handle_t handle, const void *data, size_t data_len);
void bootloader_sha256_finish(bootloader_sha256_handle_t handle, uint8_t *digest);

#endif
