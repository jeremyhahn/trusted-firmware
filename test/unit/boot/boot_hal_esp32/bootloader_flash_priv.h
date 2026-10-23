/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's bootloader_flash_priv.h: the flash calls
 * boot_hal_esp32.c makes, with ESP-IDF's signatures.
 */
#ifndef TFW_STANDIN_BOOTLOADER_FLASH_PRIV_H
#define TFW_STANDIN_BOOTLOADER_FLASH_PRIV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t   bootloader_flash_read(size_t src_addr, void *dest, size_t size, bool allow_decrypt);
esp_err_t   bootloader_flash_write(size_t dest_addr, void *src, size_t size, bool write_encrypted);
esp_err_t   bootloader_flash_erase_sector(size_t sector);
const void *bootloader_mmap(uint32_t src_addr, uint32_t size);
void        bootloader_munmap(const void *mapping);

#endif
