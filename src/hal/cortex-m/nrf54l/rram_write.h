/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L15's RRAM write, one copy for tfw-boot (boot/hal/cortex-m/nrf54l/
 * boot_rram.c) and the application's EEPROM (hal_eeprom.c here). RRAM needs
 * no erase, but a store to it programs nothing until the RRAM controller's
 * CONFIG.WEN is set, and while it is set every byte of the RRAM is writable.
 * So the caller checks its range before it calls this; this checks none.
 *
 * The sequence is the vendor's (nrfx haly/nrfy_rramc.h, nrfy_rramc_words_write
 * and nrfy_rramc_bytes_write): CONFIG.WEN with the write buffer off and wait
 * for READY; before each store wait for READYNEXT; then CONFIG back as it was
 * and READY again. The vendor's waits are unbounded. These are bounded, so a
 * controller that never answers fails the write rather than holding the core
 * until the watchdog, and CONFIG is put back on every path. A timeout can leave
 * the units before it written: the caller's range check is the only all or
 * nothing there is.
 *
 * The store goes through TFW_NRF54L_RRAM_STORE32/8, a volatile store unless a
 * host test names its RRAM model's (test/unit/test_nrf54l_eeprom.c).
 *
 * Not confirmed on the board: the bound (no write was timed).
 */
#ifndef TFW_NRF54L_RRAM_WRITE_H
#define TFW_NRF54L_RRAM_WRITE_H

#include <stdint.h>
#include <string.h>

#include "tfw/status.h"
#include "soc_regs.h"
#include "soc_tables.h"

/* Far past one store at 128 MHz; the vendor gives no figure to bound it by. */
#define TFW_NRF54L_RRAMC_SPINS 1000000u

#ifndef TFW_NRF54L_RRAM_STORE32
#define TFW_NRF54L_RRAM_STORE32(addr, word) (*(volatile uint32_t *)(uintptr_t)(addr) = (word))
#endif
#ifndef TFW_NRF54L_RRAM_STORE8
#define TFW_NRF54L_RRAM_STORE8(addr, byte)  (*(volatile uint8_t *)(uintptr_t)(addr) = (byte))
#endif

static inline uint8_t tfw_nrf54l_rramc_wait(const volatile uint32_t *reg)
{
    for (uint32_t i = 0u; i < TFW_NRF54L_RRAMC_SPINS; i++) {
        if ((*reg & TFW_NRF_RRAMC_READY_BIT) != 0u) {
            return 1u;
        }
    }
    return 0u;
}

static inline tfw_status_t tfw_nrf54l_rram_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
    tfw_status_t st = TFW_OK;
    uint32_t     prev;

    prev = TFW_NRF_RRAMC_CONFIG;
    TFW_NRF_RRAMC_CONFIG = TFW_NRF_RRAMC_CONFIG_WEN;
    tfw_cm_dsb();
    if (!tfw_nrf54l_rramc_wait(&TFW_NRF_RRAMC_READY)) {
        st = TFW_ERR_TIMEOUT;
    }

    while (st == TFW_OK && len != 0u) {
        uint32_t unit = tfw_nrf54l_rram_unit(addr, len);

        tfw_cm_dsb();
        if (!tfw_nrf54l_rramc_wait(&TFW_NRF_RRAMC_READYNEXT)) {
            st = TFW_ERR_TIMEOUT;
            break;
        }
        if (unit == 4u) {
            uint32_t word;

            memcpy(&word, src, sizeof word);    /* src need not be aligned */
            TFW_NRF54L_RRAM_STORE32(addr, word);
        } else {
            TFW_NRF54L_RRAM_STORE8(addr, *src);
        }
        addr += unit;
        src  += unit;
        len  -= unit;
    }

    tfw_cm_dsb();
    TFW_NRF_RRAMC_CONFIG = prev;
    tfw_cm_dsb();
    if (!tfw_nrf54l_rramc_wait(&TFW_NRF_RRAMC_READY) && st == TFW_OK) {
        st = TFW_ERR_TIMEOUT;
    }
    return st;
}

#endif /* TFW_NRF54L_RRAM_WRITE_H */
