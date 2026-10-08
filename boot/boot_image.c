/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "boot_image.h"

/*
 * Assembled by shift and mask, never by casting the buffer to a struct.
 *
 * The trailer arrives from flash the bootloader is about to trust, and from the
 * wire during an update. A struct overlay would make the layout depend on the
 * compiler's padding and alignment rules, and AVR's are not the host's -- so
 * the host tests would be testing a layout the device does not use. src/proto
 * does it this way for the same reason.
 */
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

tfw_status_t tfw_boot_trailer_parse(const uint8_t *raw, tfw_boot_trailer_t *out)
{
    uint32_t magic = rd_u32(&raw[TFW_BOOT_TR_MAGIC]);
    uint16_t ver   = rd_u16(&raw[TFW_BOOT_TR_VERSION]);

    if (magic != TFW_BOOT_TRAILER_MAGIC) {
        return TFW_ERR_UNSUPPORTED;
    }
    /* An exact match, not "at least". A trailer version this build does not
     * know might move a field the signature covers, and guessing at it is how a
     * bootloader ends up verifying one span and measuring another. Extending
     * means a new version number and a build that knows both. */
    if (ver != TFW_BOOT_TRAILER_VERSION) {
        return TFW_ERR_UNSUPPORTED;
    }

    out->image_version = rd_u32(&raw[TFW_BOOT_TR_IMAGE_VERSION]);
    out->image_len     = rd_u32(&raw[TFW_BOOT_TR_IMAGE_LEN]);

    if (out->image_version == 0u) {
        return TFW_ERR_CRYPTO;
    }
    /*
     * An image may not reach its own trailer.
     *
     * Without this, an image_len of the whole region would make the measurement
     * cover the signature field -- and the signing tool, measuring the same
     * span, would produce a trailer that verifies perfectly. The result is an
     * image that passes every check while the bytes actually executed are not
     * the bytes anyone signed, because changing them changes the measurement
     * which changes the signature which changes the measurement. It is a fixed
     * point that does not exist, so in practice it means the verify fails in a
     * way that looks like a toolchain bug. Bound it here and the failure names
     * itself.
     */
    if (out->image_len == 0u || out->image_len > TFW_BOOT_MAX_IMAGE_LEN) {
        return TFW_ERR_RANGE;
    }

    memcpy(out->measurement, &raw[TFW_BOOT_TR_MEASUREMENT], TFW_BOOT_MEASURE_LEN);
    memcpy(out->signature,   &raw[TFW_BOOT_TR_SIGNATURE],   TFW_BOOT_SIG_LEN);
    return TFW_OK;
}

void tfw_boot_trailer_serialise(const tfw_boot_trailer_t *tr, uint8_t *raw)
{
    memset(raw, 0, TFW_BOOT_TRAILER_LEN);
    wr_u32(&raw[TFW_BOOT_TR_MAGIC], TFW_BOOT_TRAILER_MAGIC);
    wr_u16(&raw[TFW_BOOT_TR_VERSION], TFW_BOOT_TRAILER_VERSION);
    wr_u32(&raw[TFW_BOOT_TR_IMAGE_VERSION], tr->image_version);
    wr_u32(&raw[TFW_BOOT_TR_IMAGE_LEN], tr->image_len);
    memcpy(&raw[TFW_BOOT_TR_MEASUREMENT], tr->measurement, TFW_BOOT_MEASURE_LEN);
    memcpy(&raw[TFW_BOOT_TR_SIGNATURE], tr->signature, TFW_BOOT_SIG_LEN);
}

tfw_status_t tfw_boot_rollback_permit(uint32_t version, uint32_t floor)
{
    /* Plain comparison, not a difference. `version - floor` on unsigned
     * arithmetic wraps for every downgrade and compares as an enormous positive
     * number, which is the classic way this check is written wrong and passes
     * its happy-path test. */
    if (version < floor) {
        return TFW_ERR_PROTECTED_REGION;
    }
    return TFW_OK;
}

tfw_status_t tfw_boot_floor_advance(uint32_t version, uint32_t floor, uint32_t *out)
{
    if (version <= floor) {
        *out = floor;               /* the floor never descends */
        return TFW_OK;
    }
    if ((version - floor) > (uint32_t)TFW_BOOT_FLOOR_MAX_STEP) {
        return TFW_ERR_RANGE;
    }
    *out = version;
    return TFW_OK;
}
