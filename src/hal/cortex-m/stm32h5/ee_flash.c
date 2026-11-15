/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's flash write, confined to the EEPROM's user sector
 * (ee_flash.h). The bound is here, at the only entry the application has,
 * not in its caller: hal_eeprom.c's own range check is the AVR address map's,
 * and this one holds whatever calls it.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "stm32h5/soc_regs.h"
#include "ee_flash.h"
#include "../../../../boot/hal/cortex-m/stm32h5/h5_flash.h"

_Static_assert(TFW_CM_USER_EE_BASE >= TFW_H5_FLASH_MEM
               && TFW_CM_USER_EE_BASE - TFW_H5_FLASH_MEM + TFW_CM_USER_EE_SIZE
                  <= 2u * TFW_H5_FLASH_BANK_SIZE,
               "the user sector is not inside the flash");

tfw_status_t tfw_h5_ee_flash_write(uint32_t off, const uint8_t *data, uint32_t len)
{
    if (off > (uint32_t)TFW_CM_USER_EE_SIZE || len > (uint32_t)TFW_CM_USER_EE_SIZE - off) {
        return TFW_ERR_RANGE;
    }
    return tfw_h5_flash_update(tfw_h5_ee_flash(),
                               (uint32_t)(TFW_CM_USER_EE_BASE - TFW_H5_FLASH_MEM) + off,
                               data, len);
}
