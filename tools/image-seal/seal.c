/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#include <string.h>

#include "seal.h"
#include "boot_image.h"
#include "sha256.h"
#include "tfw/config.h"

/* SHA-256 over [0, len), in the uint16_t pieces tfw_sha256_update takes. */
static void measure(const uint8_t *image, uint32_t len, uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    tfw_sha256_t ctx;
    uint32_t     off;

    tfw_sha256_init(&ctx);
    for (off = 0u; off < len; off += 4096u) {
        uint32_t k = len - off < 4096u ? len - off : 4096u;
        tfw_sha256_update(&ctx, &image[off], (uint16_t)k);
    }
    tfw_sha256_final(&ctx, out);
}

/* The questions tfw-boot asks before it measures, asked by its own code. */
static tfw_status_t read_back(const uint8_t raw[TFW_BOOT_TRAILER_LEN], tfw_boot_trailer_t *tr)
{
    tfw_status_t st = tfw_boot_trailer_parse(raw, tr);

    return st != TFW_OK ? st : tfw_boot_binding_permit(tr, (uint8_t)TFW_BOARD_ID);
}

tfw_status_t tfw_seal_tbs(const uint8_t *image, uint32_t len, uint32_t version,
                          uint8_t flags, uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN])
{
    tfw_boot_trailer_t tr, back;
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    tfw_status_t       st;

    memset(&tr, 0, sizeof tr);
    tr.image_version = version;
    tr.image_len     = len;
    tr.board_id      = (uint8_t)TFW_BOARD_ID;
    tr.image_flags   = flags;
    /* Not measured before the length is known to be in range: parse bounds it
     * by TFW_BOOT_MAX_IMAGE_LEN, and a refused image is not read at all. */
    tfw_boot_trailer_serialise(&tr, raw);
    st = read_back(raw, &back);
    if (st != TFW_OK) {
        return st;
    }
    measure(image, len, tr.measurement);
    tfw_boot_trailer_serialise(&tr, raw);
    memcpy(tbs, raw, TFW_BOOT_TR_SIGNED_LEN);
    return TFW_OK;
}

tfw_status_t tfw_seal_trailer(const uint8_t *image, uint32_t len,
                              const uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN],
                              const uint8_t sig[TFW_BOOT_SIG_LEN],
                              uint8_t trailer[TFW_BOOT_TRAILER_LEN])
{
    tfw_boot_trailer_t tr;
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    uint8_t            m[TFW_BOOT_MEASURE_LEN];
    tfw_status_t       st;

    memset(raw, 0, sizeof raw);
    memcpy(raw, tbs, TFW_BOOT_TR_SIGNED_LEN);
    memcpy(&raw[TFW_BOOT_TR_SIGNATURE], sig, TFW_BOOT_SIG_LEN);
    st = read_back(raw, &tr);
    if (st != TFW_OK) {
        return st;
    }
    if (tr.image_len != len) {
        return TFW_ERR_CRYPTO;
    }
    measure(image, len, m);
    if (memcmp(m, tr.measurement, sizeof m) != 0) {
        return TFW_ERR_CRYPTO;
    }
    memcpy(trailer, raw, TFW_BOOT_TRAILER_LEN);
    return TFW_OK;
}

uint32_t tfw_seal_stream_len(uint32_t len, uint16_t env_len)
{
    const uint32_t page = (uint32_t)TFW_OTA_PAGE_MAX;

    if (env_len != 0u) {
        len += 2u + (uint32_t)env_len;
    }
    return (len + (uint32_t)TFW_BOOT_TRAILER_LEN + page - 1u) / page * page;
}

void tfw_seal_stream(const uint8_t *image, uint32_t len,
                     const uint8_t *env, uint16_t env_len,
                     const uint8_t trailer[TFW_BOOT_TRAILER_LEN], uint8_t *out)
{
    uint32_t total = tfw_seal_stream_len(len, env_len);

    memset(out, 0xFF, total);
    memcpy(out, image, len);
    if (env_len != 0u) {
        out[len]      = (uint8_t)env_len;
        out[len + 1u] = (uint8_t)(env_len >> 8);
        memcpy(&out[len + 2u], env, env_len);
    }
    memcpy(&out[total - (uint32_t)TFW_BOOT_TRAILER_LEN], trailer, TFW_BOOT_TRAILER_LEN);
}

void tfw_seal_region(const uint8_t *image, uint32_t len,
                     const uint8_t trailer[TFW_BOOT_TRAILER_LEN], uint8_t *out)
{
    memset(out, 0xFF, TFW_BOOT_APP_SIZE);
    memcpy(out, image, len);
    memcpy(&out[TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE], trailer, TFW_BOOT_TRAILER_LEN);
}
