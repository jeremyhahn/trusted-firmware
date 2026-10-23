/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for ESP-IDF's bootloader_common.h, for test_boot_l0_esp32_mutate.c:
 * the OTA data reads and the RTC-retained record, laid out as ESP-IDF lays it
 * out with a custom reserve and CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_IN_CRC off.
 */
#ifndef TFW_TEST_L0_BOOTLOADER_COMMON_H
#define TFW_TEST_L0_BOOTLOADER_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_flash_partitions.h"

typedef struct {
    esp_partition_pos_t partition;
    uint16_t            reboot_counter;
    uint8_t             flags;
    uint8_t             version;
    uint8_t             custom[96];
    uint32_t            crc;
} rtc_retain_mem_t;

esp_err_t         bootloader_common_read_otadata(const esp_partition_pos_t *ota_info,
                                                 esp_ota_select_entry_t *two_otadata);
int               bootloader_common_get_active_otadata(esp_ota_select_entry_t *two_otadata);
rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void);
void              bootloader_common_reset_rtc_retain_mem(void);
void              bootloader_common_update_rtc_retain_mem(esp_partition_pos_t *partition,
                                                          bool reboot_counter);

#endif
