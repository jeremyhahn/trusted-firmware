/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "boot_measure.h"
#include "boot_hal.h"
#include "crc16.h"
#include "sha256.h"

/*
 * 32 bytes at a time.
 *
 * Not a page, and not one byte. A page-sized buffer would be 128 bytes on the
 * 328P and 256 on the Mega, which is an eighth of the smaller part's entire
 * SRAM spent on a buffer whose only job is to exist; one byte at a time would
 * pay a function call per byte across 28 KB. Thirty-two is a whole number of
 * SHA-256 words, costs 32 bytes of stack, and divides every page size in the
 * family so the loop has no tail case to get wrong.
 *
 * The chunk size does NOT affect the digest -- SHA-256 is defined over the byte
 * sequence, not over how it was delivered -- so a verifier reimplementing this
 * never needs to know the number. That property is worth one sentence here
 * because it is the thing someone will doubt at 2am.
 */
#define MEASURE_CHUNK 32u

void ag_boot_hash(const uint8_t *data, uint16_t len, uint8_t out[AG_BOOT_MEASURE_LEN])
{
    ag_sha256(data, len, out);
}

ag_status_t ag_boot_measure(uint32_t len, uint8_t out[AG_BOOT_MEASURE_LEN])
{
    ag_sha256_t ctx;
    uint8_t     chunk[MEASURE_CHUNK];
    uint32_t    addr = 0u;

    if (len == 0u || len > AG_BOOT_MAX_IMAGE_LEN) {
        return AG_ERR_RANGE;
    }

    ag_sha256_init(&ctx);
    while (addr < len) {
        uint32_t left = len - addr;
        uint8_t  n    = (left < (uint32_t)MEASURE_CHUNK)
                      ? (uint8_t)left : (uint8_t)MEASURE_CHUNK;
        ag_boot_flash_read(addr, chunk, n);
        ag_sha256_update(&ctx, chunk, n);
        addr += n;
    }
    ag_sha256_final(&ctx, out);
    return AG_OK;
}

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
                           uint32_t image_version)
{
    uint16_t crc;

    memset(block, 0, AG_BOOT_HANDOFF_LEN);
    wr_u32(&block[AG_BOOT_HO_MAGIC], AG_BOOT_HANDOFF_MAGIC);
    block[AG_BOOT_HO_VERSION] = AG_BOOT_HANDOFF_VERSION;
    block[AG_BOOT_HO_TIER]    = tier;
    memcpy(&block[AG_BOOT_HO_CDI],         cdi,         AG_BOOT_MEASURE_LEN);
    memcpy(&block[AG_BOOT_HO_MEASUREMENT], measurement, AG_BOOT_MEASURE_LEN);
    wr_u32(&block[AG_BOOT_HO_IMAGE_VER], image_version);

    crc = ag_crc16(block, AG_BOOT_HO_CRC);
    block[AG_BOOT_HO_CRC]      = (uint8_t)(crc & 0xFFu);
    block[AG_BOOT_HO_CRC + 1u] = (uint8_t)((crc >> 8) & 0xFFu);
}

ag_status_t ag_boot_handoff_parse(const uint8_t *block,
                                  uint8_t *tier,
                                  uint8_t cdi[AG_BOOT_MEASURE_LEN],
                                  uint8_t measurement[AG_BOOT_MEASURE_LEN],
                                  uint32_t *image_version)
{
    uint16_t crc_rx;

    if (rd_u32(&block[AG_BOOT_HO_MAGIC]) != AG_BOOT_HANDOFF_MAGIC) {
        return AG_ERR_NOT_PROVISIONED;
    }
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
    return AG_OK;
}
