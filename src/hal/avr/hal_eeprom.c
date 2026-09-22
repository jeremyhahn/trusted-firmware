/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/eeprom.h>
#include <avr/wdt.h>
#include "ag/hal.h"
#include "ag/config.h"

ag_status_t ag_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    eeprom_read_block(buf, (const void *)(uintptr_t)addr, len);
    return AG_OK;
}

ag_status_t ag_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    for (i = 0u; i < len; i++) {
        /* eeprom_update_byte, not eeprom_write_byte. It reads first and skips
         * the erase/write entirely when the value is unchanged -- which is the
         * common case for configuration, and each skipped write is 3.4 ms not
         * spent and one cell-wear cycle not used. EEPROM is rated for 100,000
         * writes per cell; a host that rewrites its config every minute would
         * reach that in ten weeks. */
        eeprom_update_byte((uint8_t *)(uintptr_t)(addr + i), buf[i]);
        /* A byte takes ~3.4 ms and the watchdog is shorter than a full block.
         * Kicking per byte is what stops a legitimate bulk write from looking
         * like a hang to the watchdog. */
        wdt_reset();
    }
    return AG_OK;
}
