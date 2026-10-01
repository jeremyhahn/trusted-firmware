/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "boot_platform.h"
#include "crc16.h"

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

uint16_t ag_boot_platform_crc(const uint8_t *raw)
{
    return ag_crc16(raw, AG_BOOT_EE_O_CRC);
}

ag_status_t ag_boot_platform_parse(const uint8_t *raw, ag_boot_platform_t *out)
{
    if (rd_u32(&raw[AG_BOOT_EE_O_MAGIC]) != AG_BOOT_EE_MAGIC) {
        return AG_ERR_NOT_PROVISIONED;
    }
    if (raw[AG_BOOT_EE_O_LAYOUT] != AG_BOOT_EE_VERSION) {
        return AG_ERR_UNSUPPORTED;
    }
    /* Integrity before interpretation. Reading the fields first and checking
     * afterwards works right up until someone moves a use of one of them above
     * the check, which is a two-line edit that reintroduces the bug silently. */
    if (ag_boot_platform_crc(raw) != rd_u16(&raw[AG_BOOT_EE_O_CRC])) {
        return AG_ERR_CRC;
    }

    memcpy(out->pubkey_hash, &raw[AG_BOOT_EE_O_PUBKEY_HASH], AG_BOOT_MEASURE_LEN);
    memcpy(out->serial,      &raw[AG_BOOT_EE_O_SERIAL],      AG_BOOT_SERIAL_LEN);
    memcpy(out->operator_pubkey, &raw[AG_BOOT_EE_O_OPERATOR_PUBKEY],
           AG_BOOT_OPERATOR_PUBKEY_LEN);
    out->rollback_floor = rd_u32(&raw[AG_BOOT_EE_O_ROLLBACK_FLOOR]);
    out->boot_count     = rd_u32(&raw[AG_BOOT_EE_O_BOOT_COUNT]);
    out->boot_state     = raw[AG_BOOT_EE_O_BOOT_STATE];
    out->ota_state      = raw[AG_BOOT_EE_O_OTA_STATE];
    out->fail_count     = rd_u16(&raw[AG_BOOT_EE_O_FAIL_COUNT]);
    return AG_OK;
}

void ag_boot_platform_serialise(const ag_boot_platform_t *in, uint8_t *raw)
{
    /* `raw` is edited in place rather than rebuilt. Everything this structure
     * does not model -- the reserved span, and anything a later layout version
     * adds there -- survives a bootloader writing back a boot count. A rebuild
     * from zero would erase it, and the loss would only show up in whatever
     * tool owned those bytes. */
    wr_u32(&raw[AG_BOOT_EE_O_MAGIC], AG_BOOT_EE_MAGIC);
    raw[AG_BOOT_EE_O_LAYOUT] = AG_BOOT_EE_VERSION;
    memcpy(&raw[AG_BOOT_EE_O_PUBKEY_HASH], in->pubkey_hash, AG_BOOT_MEASURE_LEN);
    memcpy(&raw[AG_BOOT_EE_O_SERIAL],      in->serial,      AG_BOOT_SERIAL_LEN);
    memcpy(&raw[AG_BOOT_EE_O_OPERATOR_PUBKEY], in->operator_pubkey,
           AG_BOOT_OPERATOR_PUBKEY_LEN);
    wr_u32(&raw[AG_BOOT_EE_O_ROLLBACK_FLOOR], in->rollback_floor);
    wr_u32(&raw[AG_BOOT_EE_O_BOOT_COUNT],     in->boot_count);
    raw[AG_BOOT_EE_O_BOOT_STATE] = in->boot_state;
    raw[AG_BOOT_EE_O_OTA_STATE]  = in->ota_state;
    wr_u16(&raw[AG_BOOT_EE_O_FAIL_COUNT], in->fail_count);
    wr_u16(&raw[AG_BOOT_EE_O_CRC], ag_boot_platform_crc(raw));
}
