/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The memory span the MPS2 boards' image store lives in, as a host array:
 * forced into every unit of the store suite with -include, so that
 * src/store/mem.c and boot/hal/cortex-m/boot_ota_store_cortex-m.c address it
 * exactly as they address the board's RAM. Three sectors: enough for an erase
 * that must not reach its neighbours and for the last byte of the span.
 */
#ifndef TFW_STORE_MEM_MODEL_H
#define TFW_STORE_MEM_MODEL_H

#include <stdint.h>

#define TFW_OTA_STORE_MEM_SIZE 0x3000u
#define TFW_OTA_STORE_MEM_BASE ((uintptr_t)tfw_store_mem_model)

extern uint8_t tfw_store_mem_model[TFW_OTA_STORE_MEM_SIZE];

#endif
