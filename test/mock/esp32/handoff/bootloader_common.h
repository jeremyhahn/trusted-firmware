/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF's bootloader_common.h, as far as
 * src/hal/esp32/hal_handoff.c needs it: the RTC-retained block and its
 * getter. ESP-IDF's own record sits around the custom reserve here, so a
 * test sees a pointer that lands anywhere but the reserve. The rule names
 * CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE with -D, as sdkconfig would.
 * The test that includes it defines the getter.
 */
#ifndef TFW_MOCK_BOOTLOADER_COMMON_H
#define TFW_MOCK_BOOTLOADER_COMMON_H

#include <stdint.h>

typedef struct {
    uint8_t before[16];   /* ESP-IDF's own record */
    uint8_t custom[CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE];
    uint8_t after[16];
} rtc_retain_mem_t;

rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void);

#endif
