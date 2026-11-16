/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's writes to the nRF54L15's RRAM (#17): the platform block the store
 * and the trial write, and the application region an OTA would. RRAM needs no
 * erase, but a store to it programs nothing until the RRAM controller's
 * CONFIG.WEN is set, and while it is set every byte of the RRAM is writable,
 * tfw-boot's own region included. So the range is checked again here, against
 * that region's end (tfw_nrf54l_rram_span, soc_tables.c), before the window
 * opens.
 *
 * The write itself is the vendor's sequence, bounded, in
 * src/hal/cortex-m/nrf54l/rram_write.h, which the application's EEPROM
 * (src/hal/cortex-m/nrf54l/hal_eeprom.c) shares.
 *
 * Not confirmed on the board: the bound (no write was timed), and whether the
 * RRAMC's REGION permissions as reset leaves them let the Secure core write
 * the application region and the platform block (nrf54l15_types.h,
 * RRAMC_REGION; this file sets none).
 */

#include <stdint.h>

#include "tfw/config.h"
#include "../boot_soc.h"
#include "nrf54l/soc_regs.h"
#include "nrf54l/soc_tables.h"
#include "nrf54l/rram_write.h"

#define BOOT_REGION_END ((uint32_t)TFW_BOOT_REGION_BASE + (uint32_t)TFW_BOOT_REGION_SIZE)

_Static_assert((uint32_t)TFW_FLASH_TOTAL <= TFW_NRF54L_RRAM_SIZE,
               "the board's code memory must lie in the RRAM");
_Static_assert((uint32_t)TFW_CM_PLATFORM_BASE >= BOOT_REGION_END,
               "the platform block must lie above tfw-boot's region");

tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
    if (!tfw_nrf54l_rram_span(BOOT_REGION_END, addr, len)) {
        return TFW_ERR_PROTECTED_REGION;
    }
    return tfw_nrf54l_rram_write(addr, src, len);
}
