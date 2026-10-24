/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5's bootloader_common.h: the bootloader's
 * RTC-retained block and its accessor. The fields ahead of `custom` follow
 * ESP-IDF's layout; only `custom` is read here. ESP-IDF includes `custom` only
 * under CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC, which every tfw ESP-IDF build
 * sets, so the stand-in always has it, sized by
 * CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE, which the rule names with -D.
 * test/mock/esp_sys_mock.c keeps the block.
 */
#ifndef TFW_MOCK_BOOTLOADER_COMMON_H
#define TFW_MOCK_BOOTLOADER_COMMON_H

#include <stdint.h>

typedef struct {
    uint32_t offset;
    uint32_t size;
} esp_partition_pos_t;

typedef struct {
    esp_partition_pos_t partition;
    uint16_t            reboot_counter;
    union {
        uint8_t val; /* ESP-IDF also names its bits, by an anonymous struct */
    } flags;
    uint8_t  reserve;
    uint8_t  custom[CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE];
    uint32_t crc;
} rtc_retain_mem_t;

rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void);

#endif
