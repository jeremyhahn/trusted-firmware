/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "storage.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "../core/auth_session.h"

/*
 * EEPROM, with a protected region.
 *
 * THIS IS A SECURITY BOUNDARY, not a convention. The low bytes hold the
 * bootloader's public-key hash, the device serial, the boot counter, the OTA
 * state and -- element tier only, since issue #4 -- the operator public key
 * the auth session verifies against. Without the write refusal below, the
 * generic EEPROM primitive would let anyone with access to the serial line
 * overwrite the hash the bootloader verifies against, or the operator key that
 * gates reserved-region reads -- either one the entire root of trust, defeated
 * by a documented command doing exactly what it says.
 *
 * Lock bits do not help here. They stop an external programmer reading or
 * writing the part; they say nothing about application code, and this firmware
 * IS the application. The protection has to be in the firmware or it does not
 * exist.
 *
 * READS WERE ONCE UNCONDITIONALLY PERMITTED, on the reasoning that the region
 * held only a hash and a serial number -- public by nature, so refusing to
 * read them bought nothing. That stopped being true the moment the region
 * could also hold an operator public key that GATES something: a key is still
 * public information once trusted, but a host that has not yet proven it holds
 * the matching private key has no business confirming what the anchor even is
 * before it tries, and the wire's shape should not hand out reconnaissance for
 * free. So a read below TFW_EEPROM_RESERVED_END now requires the very session
 * this region's own key anchors -- tfw_auth_gate() below -- on the tier that
 * can enforce it.
 *
 * ON THE SOFT AND CRYPTO=NONE TIERS THIS GATE IS A NO-OP: tfw_auth_gate() is
 * auth_session_none.c's permissive stub there (see TFW_FEATURE_AUTH_SESSION in
 * config.h), so reads of this region remain exactly as unconditional as they
 * always were. That is an accepted gap, not an oversight -- the only
 * software-tier verify primitive available (ed25519-donna) does not fit this
 * device family, so there is nothing on those tiers left to gate with. See
 * docs/security/threat-model.md.
 */
#define TFW_EEPROM_RESERVED_END TFW_BOOT_EE_LEN   /* [0x000, TFW_BOOT_EE_LEN) is platform-owned */

/* Chunked deliberately. A byte takes about 3.4 ms, so 64 bytes would hold the
 * main loop for 220 ms -- long enough to miss a scheduled deadline and, once a
 * deadman exists, long enough to trip it. The host writes in chunks; the device
 * stays responsive. */
#define TFW_EEPROM_CHUNK 8u

tfw_status_t tfw_storage_cmd_read(tfw_reader_t *r, tfw_writer_t *w)
{
    uint16_t addr = tfw_read_u16(r);
    uint8_t  len  = tfw_read_u8(r);
    uint8_t  buf[TFW_EEPROM_CHUNK];
    tfw_status_t st;

    if (len > TFW_EEPROM_CHUNK) {
        return TFW_ERR_RANGE;
    }
    /* Written to avoid overflowing addr + len, which on a uint16_t address and
     * a 1 KB part is reachable from a malformed request rather than theoretical. */
    if (addr >= TFW_EEPROM_SIZE || len > (uint16_t)(TFW_EEPROM_SIZE - addr)) {
        return TFW_ERR_RANGE;
    }
    /* Mirrors the write path's own reasoning below: the whole read must clear
     * the reserved region, not merely start above it. */
    if (addr < TFW_EEPROM_RESERVED_END) {
        st = tfw_auth_gate();
        if (st != TFW_OK) {
            return st;
        }
    }
    st = tfw_hal_eeprom_read(addr, buf, len);
    if (st != TFW_OK) {
        return st;
    }
    tfw_write_blob(w, buf, len);
    return TFW_OK;
}

tfw_status_t tfw_storage_cmd_write(tfw_reader_t *r, tfw_writer_t *w)
{
    uint16_t       addr = tfw_read_u16(r);
    uint8_t        len  = tfw_reader_remaining(r);
    const uint8_t *data;

    (void)w;
    if (len == 0u || len > TFW_EEPROM_CHUNK) {
        return TFW_ERR_RANGE;
    }
    if (addr >= TFW_EEPROM_SIZE || len > (uint16_t)(TFW_EEPROM_SIZE - addr)) {
        return TFW_ERR_RANGE;
    }
    /* The whole write must clear the reserved region, not merely start above
     * it. A write beginning at 0x7F and running eight bytes would otherwise
     * reach 0x86 -- past the boundary -- having passed a check on its first
     * address only. */
    if (addr < TFW_EEPROM_RESERVED_END) {
        return TFW_ERR_PROTECTED_REGION;
    }
    data = tfw_read_blob(r, len);
    if (!data) {
        return TFW_ERR_BAD_ARGC;
    }
    return tfw_hal_eeprom_write(addr, data, len);
}
