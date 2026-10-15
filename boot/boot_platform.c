/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <stddef.h>
#include <string.h>

#include "boot_platform.h"
#include "crc16.h"

/* The modelled fields are one contiguous span of the block, in the structure's
 * order, so parse and serialise copy it whole: a field-by-field copy costs the
 * 4096-byte boot section bytes it does not have. The asserts pin every field to
 * its offset; a layout change that breaks the copy fails here, not on a device.
 * The multi-byte fields rely on a little-endian target, which every supported
 * architecture is; the platform unit tests pin the byte order with literals. */
#define TFW_BOOT_EE_O_SPAN TFW_BOOT_EE_O_PUBKEY_HASH
#define TFW_BOOT_AT(field, off) \
    _Static_assert(offsetof(tfw_boot_platform_t, field) == (off) - TFW_BOOT_EE_O_SPAN, \
                   "a modelled field is not at its EEPROM offset")
TFW_BOOT_AT(pubkey_hash,     TFW_BOOT_EE_O_PUBKEY_HASH);
TFW_BOOT_AT(rollback_floor,  TFW_BOOT_EE_O_ROLLBACK_FLOOR);
TFW_BOOT_AT(boot_count,      TFW_BOOT_EE_O_BOOT_COUNT);
TFW_BOOT_AT(boot_state,      TFW_BOOT_EE_O_BOOT_STATE);
TFW_BOOT_AT(ota_state,       TFW_BOOT_EE_O_OTA_STATE);
TFW_BOOT_AT(fail_count,      TFW_BOOT_EE_O_FAIL_COUNT);
TFW_BOOT_AT(serial,          TFW_BOOT_EE_O_SERIAL);
TFW_BOOT_AT(operator_pubkey, TFW_BOOT_EE_O_OPERATOR_PUBKEY);
_Static_assert(TFW_BOOT_EE_O_SPAN + sizeof(tfw_boot_platform_t) <= TFW_BOOT_EE_O_CRC,
               "the modelled span runs into the CRC");

uint32_t tfw_boot_rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

uint16_t tfw_boot_rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void tfw_boot_wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

void tfw_boot_wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

uint16_t tfw_boot_platform_crc(const uint8_t *raw)
{
    return tfw_crc16(raw, TFW_BOOT_EE_O_CRC);
}

tfw_status_t tfw_boot_platform_parse(const uint8_t *raw, tfw_boot_platform_t *out)
{
    if (tfw_boot_rd_u32(&raw[TFW_BOOT_EE_O_MAGIC]) != TFW_BOOT_EE_MAGIC) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    if (raw[TFW_BOOT_EE_O_LAYOUT] != TFW_BOOT_EE_VERSION) {
        return TFW_ERR_UNSUPPORTED;
    }
    /* Integrity before interpretation. Reading the fields first and checking
     * afterwards works right up until someone moves a use of one of them above
     * the check, which is a two-line edit that reintroduces the bug silently. */
    if (tfw_boot_platform_crc(raw) != tfw_boot_rd_u16(&raw[TFW_BOOT_EE_O_CRC])) {
        return TFW_ERR_CRC;
    }

    memcpy(out, &raw[TFW_BOOT_EE_O_SPAN], sizeof *out);
    return TFW_OK;
}

void tfw_boot_platform_serialise(const tfw_boot_platform_t *in, uint8_t *raw)
{
    /* `raw` is edited in place rather than rebuilt. Everything this structure
     * does not model -- the reserved span, and anything a later layout version
     * adds there -- survives a bootloader writing back a boot count. A rebuild
     * from zero would erase it, and the loss would only show up in whatever
     * tool owned those bytes. */
    tfw_boot_wr_u32(&raw[TFW_BOOT_EE_O_MAGIC], TFW_BOOT_EE_MAGIC);
    raw[TFW_BOOT_EE_O_LAYOUT] = TFW_BOOT_EE_VERSION;
    memcpy(&raw[TFW_BOOT_EE_O_SPAN], in, sizeof *in);
    tfw_boot_wr_u16(&raw[TFW_BOOT_EE_O_CRC], tfw_boot_platform_crc(raw));
}
