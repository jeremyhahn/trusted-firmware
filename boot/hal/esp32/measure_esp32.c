/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The measurement in the ESP32 layer 0, over ESP-IDF's bootloader SHA-256.
 *
 * ONE HASH PER IMAGE, as measure_atecc608a.c says of the element. The
 * bootloader already links ESP-IDF's SHA-256 (bootloader_sha.c, over the SHA
 * peripheral) for esp_image_verify, and ESP-IDF's bootloader linker script
 * places it in iram_loader_seg. src/crypto/sha256_small.c beside it was a
 * second SHA-256, and it landed in iram_seg, the small region the linker
 * script fills with every .text it does not name: with micro-ecc there too,
 * the calls from that region into iram_loader_seg no longer reached
 * (R_RISCV_RVC_JUMP truncated) and the bootloader did not link.
 *
 * ANY LENGTH, AS SHA-256 IS. The SUIT manifest gate (src/core/suit.c, through
 * l0_crypto.c) hashes the wrapped manifest and its Sig_structure, whose
 * lengths are the CBOR's, not whole words; a hash that refused them gave the
 * gate a zero digest and every sealed image TFW_ERR_CRYPTO. On the C3 and the
 * P4, bootloader_sha256_data is the ROM's ets_sha_update, which keeps the
 * partial block in its context and takes any length. On the classic ESP32 it
 * is ESP-IDF's own word loop, which asserts a multiple of 4 and reads the
 * data as uint32_t, so there the hash is the ROM's ets_sha_update, which
 * takes a length in bits but no partial block before the last call (ESP-IDF
 * v5.5.5 bootloader_sha.c:29-33 and :92-132, esp32/rom/sha.h:49). Either
 * way every byte goes through an aligned word buffer.
 *
 * THE DIGEST IS IDENTICAL: SHA-256 over the same bytes in the same order, so
 * the image key's pin tfwprov bakes and the digest l0_seal.py signs are the
 * ones measure_soft.c would give. The QEMU suite's gate steps hold it to that,
 * and test_boot_measure_esp32 holds both paths to measure_soft.c on the host.
 *
 * tfw_boot_measure_from stays on bootloader_sha256_data, whole words only,
 * on every target: it refuses any other length with TFW_ERR_RANGE.
 */
#include <string.h>

#include "sdkconfig.h"
#include "bootloader_sha.h"
#include "boot_hal.h"
#include "boot_measure.h"
#if CONFIG_IDF_TARGET_ESP32
#include "rom/sha.h"
#endif

/* Bytes per hash call: a whole number of words, and the stack the word
 * buffer costs. The digest does not depend on it. On the classic ESP32 it is
 * the SHA-256 block, so every call but the last is whole blocks. */
#if CONFIG_IDF_TARGET_ESP32
#define CHUNK 64u
#else
#define CHUNK 32u
#endif

void tfw_boot_hash(const uint8_t *data, uint16_t len, uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    uint32_t                   w[CHUNK / 4u];
    uint16_t                   off = 0u;
#if CONFIG_IDF_TARGET_ESP32
    SHA_CTX                    ctx;

    ets_sha_enable();
    ets_sha_init(&ctx);
#else
    bootloader_sha256_handle_t h = bootloader_sha256_start();
#endif

    while (off < len) {
        uint16_t n = (uint16_t)(len - off) < (uint16_t)CHUNK
                     ? (uint16_t)(len - off) : (uint16_t)CHUNK;

        memcpy(w, &data[off], n);
#if CONFIG_IDF_TARGET_ESP32
        ets_sha_update(&ctx, SHA2_256, (const uint8_t *)w, (size_t)n * 8u);
#else
        bootloader_sha256_data(h, w, n);
#endif
        off = (uint16_t)(off + n);
    }
#if CONFIG_IDF_TARGET_ESP32
    ets_sha_finish(&ctx, SHA2_256, out);
#else
    bootloader_sha256_finish(h, out);
#endif
    memset(w, 0, sizeof w);
}

static tfw_status_t flash(uint32_t addr, uint8_t *buf, uint8_t len)
{
    tfw_boot_flash_read(addr, buf, len);
    return TFW_OK;
}

tfw_status_t tfw_boot_measure(uint32_t len, uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    return tfw_boot_measure_from(flash, TFW_BOOT_APP_BASE, len, out);
}

tfw_status_t tfw_boot_measure_from(tfw_boot_reader_t rd, uint32_t base, uint32_t len,
                                   uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    uint32_t                   w[CHUNK / 4u];
    bootloader_sha256_handle_t h;
    uint32_t                   addr = 0u;
    tfw_status_t               st   = TFW_OK;

    if (len == 0u || len > TFW_BOOT_MAX_IMAGE_LEN || (len & 3u) != 0u) {
        return TFW_ERR_RANGE;
    }
    h = bootloader_sha256_start();
    while (st == TFW_OK && addr < len) {
        uint32_t left = len - addr;
        uint8_t  n    = (left < (uint32_t)CHUNK) ? (uint8_t)left : (uint8_t)CHUNK;

        st = rd(base + addr, (uint8_t *)w, n);
        if (st == TFW_OK) {
            bootloader_sha256_data(h, w, n);
        }
        addr += n;
    }
    /* A NULL digest ends the hash and gives nothing: a read that failed is
     * no measurement. */
    bootloader_sha256_finish(h, st == TFW_OK ? out : 0);
    memset(w, 0, sizeof w);
    return st;
}
