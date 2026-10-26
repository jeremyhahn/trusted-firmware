/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's flash writes on the MCXN947: boot_soc.h's entry, over the
 * board's 2 MiB of flash (TFW_FLASH_TOTAL, board_profile.h), through the boot
 * ROM's flash driver. The driver is the fifth word of the ROM's API tree
 * (mcxn_flash.h has where each value is in the SDK); mcxn_flash.c has the
 * rest.
 *
 * That the driver may be called from code running in the flash it writes is
 * the SDK's fsl_flash.c calling it so, not a manual read
 * (docs/boards/frdm-mcxn947.md). tfw-boot enables no interrupt; the mask is
 * held across the calls all the same.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "../boot_soc.h"
#include "mcxn_flash.h"
#include "cm_regs.h"

tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    const volatile uint32_t *tree = (const volatile uint32_t *)(uintptr_t)TFW_MCXN_ROM_API_TREE;
    const tfw_mcxn_flash_api_t *api =
        (const tfw_mcxn_flash_api_t *)(uintptr_t)tree[TFW_MCXN_ROM_TREE_FLASH];
    uint32_t primask = tfw_cm_irq_save();
    tfw_status_t st;

    st = tfw_mcxn_flash_update(api, (const uint8_t *)(uintptr_t)TFW_MCXN_FLASH_BASE,
                               (uint32_t)TFW_FLASH_TOTAL, addr, data, len);
    /* The writes complete before anything reads the flash they wrote. */
    tfw_cm_dsb();
    tfw_cm_isb();
    tfw_cm_irq_restore(primask);
    return st;
}
