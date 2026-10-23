/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's sdkconfig.h, for test_boot_hal_esp32 only.
 * The one option boot_hal_esp32.c reads, CONFIG_IDF_TARGET_ESP32P4, comes
 * from the make rule: 0 for the C3's reset names, 1 for the P4's.
 */
#ifndef TFW_STANDIN_SDKCONFIG_H
#define TFW_STANDIN_SDKCONFIG_H


#endif
