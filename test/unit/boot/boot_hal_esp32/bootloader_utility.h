/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's bootloader_utility.h: the loader's three
 * calls and bootloader_reset. INVALID_INDEX is ESP-IDF's (bootloader_config.h).
 * bootloader_reset does not return on the part; the stand-in records the call
 * and returns, which boot_hal.h's contract allows the host.
 */
#ifndef TFW_STANDIN_BOOTLOADER_UTILITY_H
#define TFW_STANDIN_BOOTLOADER_UTILITY_H

#include <stdbool.h>

#define INVALID_INDEX (-99)

typedef struct {
    int app_count;
} bootloader_state_t;

bool bootloader_utility_load_partition_table(bootloader_state_t *bs);
int  bootloader_utility_get_selected_boot_partition(const bootloader_state_t *bs);
void bootloader_utility_load_boot_image(const bootloader_state_t *bs, int start_index);
void bootloader_reset(void);

#endif
