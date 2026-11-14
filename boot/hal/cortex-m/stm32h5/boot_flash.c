/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's flash write on the STM32H5: h5_flash.c's driver on the part's own
 * flash interface and flash, and the wait it leaves to this file. The host
 * test links its own wait instead (test/unit/boot/test_boot_stm32h5_flash.c).
 *
 * The instruction cache (ICACHE) is off at reset and tfw-boot never enables
 * it, so nothing it holds can go stale here.
 */

#include <stdint.h>

#include "../boot_soc.h"
#include "stm32h5/soc_regs.h"
#include "h5_flash.h"

void tfw_h5_flash_wait(const struct tfw_h5_flash *f)
{
    while ((f->regs[TFW_H5_FLASH_NSSR_OFF / 4u] &
            (TFW_H5_FLASH_SR_BSY | TFW_H5_FLASH_SR_WBNE | TFW_H5_FLASH_SR_DBNE)) != 0u) {
    }
}

uint32_t tfw_h5_flash_irq_save(void)
{
    return tfw_cm_irq_save();
}

void tfw_h5_flash_irq_restore(uint32_t mask)
{
    tfw_cm_irq_restore(mask);
}

tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    static const struct tfw_h5_flash flash = {
        (volatile uint32_t *)(uintptr_t)TFW_H5_FLASH_BASE,
        (uint8_t *)(uintptr_t)TFW_H5_FLASH_MEM,
    };
    tfw_status_t st;

    if (addr < TFW_H5_FLASH_MEM) {
        return TFW_ERR_RANGE;
    }
    st = tfw_h5_flash_update(&flash, addr - TFW_H5_FLASH_MEM, data, len);
    tfw_cm_dsb();
    return st;
}
