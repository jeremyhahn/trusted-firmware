/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Stand-in for ESP-IDF's bootloader_init.h, for test_boot_l0_esp32_mutate.c. */
#ifndef TFW_TEST_L0_BOOTLOADER_INIT_H
#define TFW_TEST_L0_BOOTLOADER_INIT_H

#include "esp_err.h"

esp_err_t bootloader_init(void);

#endif
