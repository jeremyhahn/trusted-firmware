/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ee_flash.c on the part: its own flash interface and flash, and the wait and
 * interrupt mask h5_flash.c leaves to its caller. The host test links its
 * register model's instead (test/unit/test_stm32h5_eeprom.c).
 */

#include <stdint.h>

#include "cm_regs.h"
#include "stm32h5/soc_regs.h"
#include "ee_flash.h"
#include "../../../../boot/hal/cortex-m/stm32h5/h5_flash.h"

const struct tfw_h5_flash *tfw_h5_ee_flash(void)
{
    static const struct tfw_h5_flash flash = {
        (volatile uint32_t *)(uintptr_t)TFW_H5_FLASH_BASE,
        (uint8_t *)(uintptr_t)TFW_H5_FLASH_MEM,
    };

    return &flash;
}

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

/* The stores complete before the mask comes off. */
void tfw_h5_flash_irq_restore(uint32_t mask)
{
    tfw_cm_dsb();
    tfw_cm_irq_restore(mask);
}
