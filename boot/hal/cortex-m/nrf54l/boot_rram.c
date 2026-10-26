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
 * The sequence is the vendor's (nrfx haly/nrfy_rramc.h, nrfy_rramc_words_write
 * and nrfy_rramc_bytes_write): CONFIG.WEN with the write buffer off and wait
 * for READY; before each store wait for READYNEXT; then CONFIG back as it was
 * and READY again. The vendor's waits are unbounded. These are bounded, so a
 * controller that never answers fails the write rather than holding tfw-boot
 * until the watchdog, and CONFIG is put back on every path.
 *
 * Not confirmed on the board: the bound (no write was timed), and whether the
 * RRAMC's REGION permissions as reset leaves them let the Secure core write
 * the application region and the platform block (nrf54l15_types.h,
 * RRAMC_REGION; this file sets none).
 */

#include <stdint.h>
#include <string.h>

#include "tfw/config.h"
#include "../boot_soc.h"
#include "nrf54l/soc_regs.h"
#include "nrf54l/soc_tables.h"

/* Far past one store at 128 MHz; the vendor gives no figure to bound it by. */
#define RRAMC_SPINS 1000000u

#define BOOT_REGION_END ((uint32_t)TFW_BOOT_REGION_BASE + (uint32_t)TFW_BOOT_REGION_SIZE)

_Static_assert((uint32_t)TFW_FLASH_TOTAL <= TFW_NRF54L_RRAM_SIZE,
               "the board's code memory must lie in the RRAM");
_Static_assert((uint32_t)TFW_CM_PLATFORM_BASE >= BOOT_REGION_END,
               "the platform block must lie above tfw-boot's region");

static uint8_t rramc_wait(const volatile uint32_t *reg)
{
    for (uint32_t i = 0u; i < RRAMC_SPINS; i++) {
        if ((*reg & TFW_NRF_RRAMC_READY_BIT) != 0u) {
            return 1u;
        }
    }
    return 0u;
}

tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
    tfw_status_t st = TFW_OK;
    uint32_t     prev;

    if (!tfw_nrf54l_rram_span(BOOT_REGION_END, addr, len)) {
        return TFW_ERR_PROTECTED_REGION;
    }

    prev = TFW_NRF_RRAMC_CONFIG;
    TFW_NRF_RRAMC_CONFIG = TFW_NRF_RRAMC_CONFIG_WEN;
    tfw_cm_dsb();
    if (!rramc_wait(&TFW_NRF_RRAMC_READY)) {
        st = TFW_ERR_TIMEOUT;
    }

    while (st == TFW_OK && len != 0u) {
        uint32_t unit = tfw_nrf54l_rram_unit(addr, len);

        tfw_cm_dsb();
        if (!rramc_wait(&TFW_NRF_RRAMC_READYNEXT)) {
            st = TFW_ERR_TIMEOUT;
            break;
        }
        if (unit == 4u) {
            uint32_t word;

            memcpy(&word, src, sizeof word);    /* src need not be aligned */
            *(volatile uint32_t *)(uintptr_t)addr = word;
        } else {
            *(volatile uint8_t *)(uintptr_t)addr = *src;
        }
        addr += unit;
        src  += unit;
        len  -= unit;
    }

    tfw_cm_dsb();
    TFW_NRF_RRAMC_CONFIG = prev;
    tfw_cm_dsb();
    if (!rramc_wait(&TFW_NRF_RRAMC_READY) && st == TFW_OK) {
        st = TFW_ERR_TIMEOUT;
    }
    return st;
}
