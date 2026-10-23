/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for ESP-IDF's bootloader_utility.h with its bootloader_config.h,
 * for test_boot_l0_esp32_mutate.c: the partition state and ESP-IDF's index
 * values. The reset and the load do not return; the test's longjmp back.
 */
#ifndef TFW_TEST_L0_BOOTLOADER_UTILITY_H
#define TFW_TEST_L0_BOOTLOADER_UTILITY_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_flash_partitions.h"

#define MAX_OTA_SLOTS  16
#define FACTORY_INDEX  (-1)
#define TEST_APP_INDEX (-2)
#define INVALID_INDEX  (-99)

typedef struct {
    esp_partition_pos_t ota_info;
    esp_partition_pos_t factory;
    esp_partition_pos_t test;
    esp_partition_pos_t ota[MAX_OTA_SLOTS];
    uint32_t            app_count;
    uint32_t            selected_subtype;
} bootloader_state_t;

bool bootloader_utility_load_partition_table(bootloader_state_t *bs);
int  bootloader_utility_get_selected_boot_partition(const bootloader_state_t *bs);
void bootloader_utility_load_boot_image(const bootloader_state_t *bs, int start_index)
    __attribute__((__noreturn__));
void bootloader_reset(void) __attribute__((__noreturn__));

#endif
