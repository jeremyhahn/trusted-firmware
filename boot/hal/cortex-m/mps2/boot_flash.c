/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's flash write on mps2-tz: the code regions are SRAM that QEMU
 * loads the images into, so a write is a copy, finished before it returns.
 */

#include <stdint.h>
#include <string.h>

#include "../boot_soc.h"
#include "cm_regs.h"

tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    memcpy((void *)(uintptr_t)addr, data, len);
    tfw_cm_dsb();
    return TFW_OK;
}
