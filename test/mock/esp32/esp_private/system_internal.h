/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5's esp_private/system_internal.h: the
 * system reset the classic ESP32 takes in tfw_hal_reset. ESP-IDF declares it
 * noreturn; the stand-in does not, so a mock may record the call and return.
 * The host build is not CONFIG_IDF_TARGET_ESP32, so nothing calls it there
 * and nothing defines it.
 */
#ifndef TFW_MOCK_ESP_SYSTEM_INTERNAL_H
#define TFW_MOCK_ESP_SYSTEM_INTERNAL_H

void esp_restart_noos_dig(void);

#endif
