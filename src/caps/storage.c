/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "storage.h"
#include "ag/hal.h"
#include "ag/config.h"

/*
 * EEPROM, with a protected region.
 *
 * THIS IS A SECURITY BOUNDARY, not a convention. The low 128 bytes hold the
 * bootloader's public-key hash, the device serial, the boot counter and the OTA
 * state. Without this check, the generic EEPROM primitive would let anyone with
 * access to the serial line overwrite the hash the bootloader verifies against
 * -- which is the entire root of trust, defeated by a documented command doing
 * exactly what it says.
 *
 * Lock bits do not help here. They stop an external programmer reading or
 * writing the part; they say nothing about application code, and this firmware
 * IS the application. The protection has to be in the firmware or it does not
 * exist.
 *
 * Reads of the region are permitted. Its contents are public -- a hash and a
 * serial number -- and refusing them would break provisioning tools for no gain.
 */
#define AG_EEPROM_RESERVED_END 0x80u   /* [0x000, 0x080) is platform-owned */

#ifndef AG_EEPROM_SIZE
#define AG_EEPROM_SIZE 1024u
#endif

/* Chunked deliberately. A byte takes about 3.4 ms, so 64 bytes would hold the
 * main loop for 220 ms -- long enough to miss a scheduled deadline and, once a
 * deadman exists, long enough to trip it. The host writes in chunks; the device
 * stays responsive. */
#define AG_EEPROM_CHUNK 8u

ag_status_t ag_storage_cmd_read(ag_reader_t *r, ag_writer_t *w)
{
    uint16_t addr = ag_read_u16(r);
    uint8_t  len  = ag_read_u8(r);
    uint8_t  buf[AG_EEPROM_CHUNK];
    ag_status_t st;

    if (len > AG_EEPROM_CHUNK) {
        return AG_ERR_RANGE;
    }
    /* Written to avoid overflowing addr + len, which on a uint16_t address and
     * a 1 KB part is reachable from a malformed request rather than theoretical. */
    if (addr >= AG_EEPROM_SIZE || len > (uint16_t)(AG_EEPROM_SIZE - addr)) {
        return AG_ERR_RANGE;
    }
    st = ag_hal_eeprom_read(addr, buf, len);
    if (st != AG_OK) {
        return st;
    }
    ag_write_blob(w, buf, len);
    return AG_OK;
}

ag_status_t ag_storage_cmd_write(ag_reader_t *r, ag_writer_t *w)
{
    uint16_t       addr = ag_read_u16(r);
    uint8_t        len  = ag_reader_remaining(r);
    const uint8_t *data;

    (void)w;
    if (len == 0u || len > AG_EEPROM_CHUNK) {
        return AG_ERR_RANGE;
    }
    if (addr >= AG_EEPROM_SIZE || len > (uint16_t)(AG_EEPROM_SIZE - addr)) {
        return AG_ERR_RANGE;
    }
    /* The whole write must clear the reserved region, not merely start above
     * it. A write beginning at 0x7F and running eight bytes would otherwise
     * reach 0x86 -- past the boundary -- having passed a check on its first
     * address only. */
    if (addr < AG_EEPROM_RESERVED_END) {
        return AG_ERR_PROTECTED_REGION;
    }
    data = ag_read_blob(r, len);
    if (!data) {
        return AG_ERR_BAD_ARGC;
    }
    return ag_hal_eeprom_write(addr, data, len);
}
