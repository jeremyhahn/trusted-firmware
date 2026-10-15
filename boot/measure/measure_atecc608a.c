/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The measurement, computed by the element: BOOT_MEASURE=atecc608a.
 *
 * WHY THIS EXISTS, in one number. A 4 KB boot section cannot hold a software
 * SHA-256 and a boot decision together: the hash alone is 4 052 bytes and the
 * smallest useful core around it is 5 688. Moving the hash into the element
 * takes the software implementation out of the boot section entirely, and what
 * is left fits. That is the whole argument -- see docs/secure-boot/architecture.md
 * for the measurements.
 *
 * WHAT IT COSTS, stated rather than discovered. The element hashes 64 bytes per
 * command, so a 28 KB application is about 450 round trips, each a bus transfer
 * plus the part's own SHA execution time. That is seconds of boot, every boot,
 * and it cannot be cached: a measurement is only evidence if it was taken this
 * time. On a part with an 8 KB boot section, use the software hash instead --
 * it is the same digest, several hundred times faster.
 *
 * THE DIGEST IS IDENTICAL. SHA-256 is defined over the byte sequence, not over
 * how the bytes were delivered, so an image measured here and an image measured
 * by measure_soft.c produce the same 32 bytes -- and the same 32 bytes a
 * verifier computes from source. That is what makes this a build-time choice
 * rather than a fork of the wire contract, and the host suite asserts it
 * against the same vectors.
 */

#include <string.h>

#include "../boot_measure.h"
#include "../boot_hal.h"
#include "atecc.h"

/* 64 bytes: the element's SHA block, and the only size its Update command
 * accepts. Not a tuning parameter. */
#define SHA_BLOCK 64u


void tfw_boot_hash(const uint8_t *data, uint16_t len, uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    /*
     * The public-key hash goes through the element too, for one reason: if it
     * did not, the software SHA-256 would still be linked into the boot section
     * and this file would save nothing at all. One hash implementation per
     * image, and on this configuration it is the element's.
     */
    uint8_t     off = 0u;
    tfw_status_t st;

    memset(out, 0, TFW_BOOT_MEASURE_LEN);
    if (tfw_atecc_wake() != TFW_OK) {
        return;
    }
    st = tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_SHA256_START, 0u,
                       0, 0u, 0, 0u, TFW_ATECC_T_SHA);
    while (st == TFW_OK && (uint16_t)(len - off) > SHA_BLOCK) {
        st = tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_SHA256_UPDATE,
                           SHA_BLOCK, &data[off], SHA_BLOCK, 0, 0u, TFW_ATECC_T_SHA);
        off = (uint8_t)(off + SHA_BLOCK);
    }
    if (st == TFW_OK) {
        uint8_t tail = (uint8_t)(len - off);
        (void)tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_SHA256_END, tail,
                            tail ? &data[off] : 0, tail, out,
                            TFW_BOOT_MEASURE_LEN, TFW_ATECC_T_SHA);
    }
    (void)tfw_atecc_sleep();
}

tfw_status_t tfw_boot_measure(uint32_t len, uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    uint8_t     block[SHA_BLOCK];
    uint32_t    addr = 0u;
    tfw_status_t st;

    if (len == 0u || len > TFW_BOOT_MAX_IMAGE_LEN) {
        return TFW_ERR_RANGE;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        /* No element means no hash on this configuration, and therefore no
         * measurement. Returning a zero digest would be a measurement of
         * nothing that the caller would treat as a measurement of something. */
        return TFW_ERR_CRYPTO;
    }

    st = tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_SHA256_START, 0u,
                       0, 0u, 0, 0u, TFW_ATECC_T_SHA);

    /* Whole blocks first, then whatever is left. The element pads; we do not,
     * because a second padding implementation is a second chance to disagree
     * with FIPS 180-4 about a digest a verifier will recompute. */
    while (st == TFW_OK && (len - addr) > (uint32_t)SHA_BLOCK) {
        tfw_boot_flash_read(TFW_BOOT_APP_BASE + addr, block, SHA_BLOCK);
        st = tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_SHA256_UPDATE,
                           SHA_BLOCK, block, SHA_BLOCK, 0, 0u, TFW_ATECC_T_SHA);
        addr += SHA_BLOCK;
    }
    if (st == TFW_OK) {
        uint8_t tail = (uint8_t)(len - addr);
        tfw_boot_flash_read(TFW_BOOT_APP_BASE + addr, block, tail);
        st = tfw_atecc_exec(TFW_ATECC_OP_SHA, TFW_ATECC_SHA_SHA256_END, tail,
                           block, tail, out, TFW_BOOT_MEASURE_LEN, TFW_ATECC_T_SHA);
    }
    memset(block, 0, sizeof block);
    (void)tfw_atecc_sleep();
    return st;
}
