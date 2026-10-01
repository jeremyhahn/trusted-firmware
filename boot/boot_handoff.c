/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The handoff block: how the CDI reaches the application.
 *
 * Its own translation unit, and not part of the measurement, because the
 * measurement has two implementations -- software and element -- and the
 * handoff has one. Leaving it beside the software hash meant the element
 * configuration linked no handoff at all, which the linker caught and a reader
 * would not have.
 *
 * Pure functions over a byte layout, so the block the application will parse is
 * testable with no MCU on either side of it.
 */

#include <string.h>

#include "boot_measure.h"
#include "crc16.h"

/* ------------------------------------------------------------- handoff */

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

void ag_boot_handoff_build(uint8_t *block,
                           uint8_t tier,
                           const uint8_t cdi[AG_BOOT_MEASURE_LEN],
                           const uint8_t measurement[AG_BOOT_MEASURE_LEN],
                           uint32_t image_version,
                           uint8_t reset_cause)
{
    uint16_t crc;

    memset(block, 0, AG_BOOT_HANDOFF_LEN);
    wr_u32(&block[AG_BOOT_HO_MAGIC], AG_BOOT_HANDOFF_MAGIC);
    block[AG_BOOT_HO_VERSION] = AG_BOOT_HANDOFF_VERSION;
    block[AG_BOOT_HO_TIER]    = tier;
    memcpy(&block[AG_BOOT_HO_CDI],         cdi,         AG_BOOT_MEASURE_LEN);
    memcpy(&block[AG_BOOT_HO_MEASUREMENT], measurement, AG_BOOT_MEASURE_LEN);
    wr_u32(&block[AG_BOOT_HO_IMAGE_VER], image_version);
    block[AG_BOOT_HO_RESET_CAUSE] = reset_cause;

    /* Version 2 widens CRC coverage to include reset_cause and the byte beside
     * it -- see include/ag/boot.h for why the CRC moved rather than the block
     * growing. */
    crc = ag_crc16(block, AG_BOOT_HO_CRC);
    block[AG_BOOT_HO_CRC]      = (uint8_t)(crc & 0xFFu);
    block[AG_BOOT_HO_CRC + 1u] = (uint8_t)((crc >> 8) & 0xFFu);
}

ag_status_t ag_boot_handoff_parse(const uint8_t *block,
                                  uint8_t *tier,
                                  uint8_t cdi[AG_BOOT_MEASURE_LEN],
                                  uint8_t measurement[AG_BOOT_MEASURE_LEN],
                                  uint32_t *image_version,
                                  uint8_t *reset_cause)
{
    uint16_t crc_rx;

    if (rd_u32(&block[AG_BOOT_HO_MAGIC]) != AG_BOOT_HANDOFF_MAGIC) {
        return AG_ERR_NOT_PROVISIONED;
    }
    /* An exact match, not a minimum. A version this build does not recognise
     * is refused rather than read with the current offsets guessed onto it --
     * see include/ag/boot.h for why that degrades exactly like no handoff at
     * all rather than like a block misread as something it is not. */
    if (block[AG_BOOT_HO_VERSION] != AG_BOOT_HANDOFF_VERSION) {
        return AG_ERR_UNSUPPORTED;
    }
    crc_rx = (uint16_t)((uint16_t)block[AG_BOOT_HO_CRC]
                        | ((uint16_t)block[AG_BOOT_HO_CRC + 1u] << 8));
    if (ag_crc16(block, AG_BOOT_HO_CRC) != crc_rx) {
        return AG_ERR_CRC;
    }

    *tier = block[AG_BOOT_HO_TIER];
    memcpy(cdi,         &block[AG_BOOT_HO_CDI],         AG_BOOT_MEASURE_LEN);
    memcpy(measurement, &block[AG_BOOT_HO_MEASUREMENT], AG_BOOT_MEASURE_LEN);
    *image_version = rd_u32(&block[AG_BOOT_HO_IMAGE_VER]);
    *reset_cause   = block[AG_BOOT_HO_RESET_CAUSE];
    return AG_OK;
}
