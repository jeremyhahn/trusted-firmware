/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "boot_platform.h"

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
tfw_status_t tfw_boot_trailer_parse(const uint8_t *raw, tfw_boot_trailer_t *out)
{
    uint32_t magic = tfw_boot_rd_u32(&raw[TFW_BOOT_TR_MAGIC]);
    uint16_t ver   = tfw_boot_rd_u16(&raw[TFW_BOOT_TR_VERSION]);

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

    /* Checked in locals, so a refused trailer writes nothing to *out. */
    uint32_t image_version = tfw_boot_rd_u32(&raw[TFW_BOOT_TR_IMAGE_VERSION]);
    uint32_t image_len     = tfw_boot_rd_u32(&raw[TFW_BOOT_TR_IMAGE_LEN]);

    if (image_version == 0u) {
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
    if (image_len == 0u || image_len > TFW_BOOT_MAX_IMAGE_LEN) {
        return TFW_ERR_RANGE;
    }

    out->image_version = image_version;
    out->image_len     = image_len;
    out->board_id      = raw[TFW_BOOT_TR_BOARD_ID];
    out->image_flags   = raw[TFW_BOOT_TR_IMAGE_FLAGS];

    memcpy(out->measurement, &raw[TFW_BOOT_TR_MEASUREMENT], TFW_BOOT_MEASURE_LEN);
    memcpy(out->signature,   &raw[TFW_BOOT_TR_SIGNATURE],   TFW_BOOT_SIG_LEN);
    return TFW_OK;
}

void tfw_boot_trailer_serialise(const tfw_boot_trailer_t *tr, uint8_t *raw)
{
    memset(raw, 0, TFW_BOOT_TRAILER_LEN);
    tfw_boot_wr_u32(&raw[TFW_BOOT_TR_MAGIC], TFW_BOOT_TRAILER_MAGIC);
    tfw_boot_wr_u16(&raw[TFW_BOOT_TR_VERSION], TFW_BOOT_TRAILER_VERSION);
    raw[TFW_BOOT_TR_BOARD_ID]    = tr->board_id;
    raw[TFW_BOOT_TR_IMAGE_FLAGS] = tr->image_flags;
    tfw_boot_wr_u32(&raw[TFW_BOOT_TR_IMAGE_VERSION], tr->image_version);
    tfw_boot_wr_u32(&raw[TFW_BOOT_TR_IMAGE_LEN], tr->image_len);
    memcpy(&raw[TFW_BOOT_TR_MEASUREMENT], tr->measurement, TFW_BOOT_MEASURE_LEN);
    memcpy(&raw[TFW_BOOT_TR_SIGNATURE], tr->signature, TFW_BOOT_SIG_LEN);
}

tfw_status_t tfw_boot_binding_permit(const tfw_boot_trailer_t *tr, uint8_t board_id)
{
    /* Zero is checked on its own, not left to the comparison, so that a part
     * built without a board profile -- TFW_BOARD_ID 0 -- cannot accept the
     * unbound images it would otherwise match. */
    if (tr->board_id == 0u || tr->board_id != board_id) {
        return TFW_ERR_UNSUPPORTED;
    }
    if ((tr->image_flags & (uint8_t)~TFW_BOOT_IMAGE_FLAGS_KNOWN) != 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    return TFW_OK;
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
