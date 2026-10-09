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

void tfw_boot_hash(const uint8_t *data, uint16_t len, uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    tfw_sha256(data, len, out);
}

tfw_status_t tfw_boot_measure(uint32_t len, uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    tfw_sha256_t ctx;
    uint8_t     chunk[MEASURE_CHUNK];
    uint32_t    addr = 0u;

    if (len == 0u || len > TFW_BOOT_MAX_IMAGE_LEN) {
        return TFW_ERR_RANGE;
    }

    tfw_sha256_init(&ctx);
    while (addr < len) {
        uint32_t left = len - addr;
        uint8_t  n    = (left < (uint32_t)MEASURE_CHUNK)
                      ? (uint8_t)left : (uint8_t)MEASURE_CHUNK;
        tfw_boot_flash_read(TFW_BOOT_APP_BASE + addr, chunk, n);
        tfw_sha256_update(&ctx, chunk, n);
        addr += n;
    }
    tfw_sha256_final(&ctx, out);
    return TFW_OK;
}
