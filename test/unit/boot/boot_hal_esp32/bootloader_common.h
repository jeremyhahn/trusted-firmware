/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's bootloader_common.h: the RTC-retained record.
 * custom[] is the size the boot's sdkconfig reserves, TFW_BOOT_HANDOFF_LEN;
 * guard[] after it is the test's, so a copy past custom[] shows.
 */
#ifndef TFW_STANDIN_BOOTLOADER_COMMON_H
#define TFW_STANDIN_BOOTLOADER_COMMON_H

#include <stdint.h>

typedef struct {
    uint8_t custom[112];
    uint8_t guard[8];
} rtc_retain_mem_t;

rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void);

#endif
