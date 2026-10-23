/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_private/system_internal.h, for
 * test/unit/test_esp_sys_mutate.c alone: the system reset
 * src/hal/esp32/hal_sys.c takes on the ESP32. On the part it does not return;
 * the test's does, and counts.
 */
#ifndef TFW_MOCK_ESP32_SYS_SYSTEM_INTERNAL_H
#define TFW_MOCK_ESP32_SYS_SYSTEM_INTERNAL_H

void esp_restart_noos_dig(void);

#endif
