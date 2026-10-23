/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's esp_rom_sys.h: the ROM's reset reason.
 */
#ifndef TFW_STANDIN_ESP_ROM_SYS_H
#define TFW_STANDIN_ESP_ROM_SYS_H

#include "soc/reset_reasons.h"

soc_reset_reason_t esp_rom_get_reset_reason(int cpu_no);

#endif
