/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OTA_ACTIVATE's manifest check on AVR (owner ruling 121). tfw-boot verifies
 * the image's P-256 signature on every row; the SUIT subset's checks run here,
 * in the application, before the MARK is written -- the split the 2560's
 * bootloader budget forced (docs/ota/engineering.md).
 *
 * The stream carries the envelope after the image, in the padding before the
 * trailer: a little-endian u16 length, then the envelope. It is the layout the
 * ESP32's layer 0 reads after its trailer (boot/hal/esp32/l0_gate.c), and
 * tfw-boot's copy leaves that padding erased in the region (boot/boot_stage.c).
 * The checks are src/core/suit.c's, the one reader both call.
 */
#include "tfw/config.h"
#include "boot_image.h"
#include "ota_stage.h"
#include "ota_store.h"
#include "suit.h"

/* In pieces: the store moves at most TFW_STORE_CHUNK bytes a read. */
static tfw_status_t get(uint32_t at, uint8_t *buf, uint16_t len)
{
    uint16_t     off;
    uint16_t     k;
    tfw_status_t st = TFW_OK;

    for (off = 0u; off < len && st == TFW_OK; off = (uint16_t)(off + k)) {
        k = (uint16_t)(len - off);
        if (k > TFW_STORE_CHUNK) {
            k = TFW_STORE_CHUNK;
        }
        st = tfw_store_read(at + off, &buf[off], (uint8_t)k);
    }
    return st;
}

tfw_status_t tfw_ota_manifest(tfw_boot_platform_t *p)
{
    uint8_t            buf[TFW_SUIT_ENVELOPE_MAX];
    uint8_t            digest[TFW_BOOT_MEASURE_LEN];
    tfw_boot_trailer_t tr;
    uint32_t           tot;
    uint32_t           at;
    uint32_t           room;
    uint16_t           len;
    tfw_status_t       st;

    st = tfw_stage_session(&tot, digest);
    if (st != TFW_OK) {
        return st;
    }
    /* OTA_END checked this trailer; the buffer holds it before the envelope. */
    st = get(((uint32_t)TFW_OTA_STORE_STAGE_ADDR) + tot - TFW_BOOT_TRAILER_LEN,
             buf, (uint16_t)TFW_BOOT_TRAILER_LEN);
    if (st == TFW_OK) {
        st = tfw_boot_trailer_parse(buf, &tr);
    }
    if (st != TFW_OK) {
        return st;
    }
    room = tot - TFW_BOOT_TRAILER_LEN - tr.image_len;
    at   = ((uint32_t)TFW_OTA_STORE_STAGE_ADDR) + tr.image_len;
    if (room < 2u) {
        return TFW_ERR_RANGE;
    }
    st = get(at, buf, 2u);
    if (st != TFW_OK) {
        return st;
    }
    len = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
    /* Erased padding reads 0xFFFF: no envelope is a refusal, as on the ESP32. */
    if (len == 0u || len > TFW_SUIT_ENVELOPE_MAX || len > room - 2u) {
        return TFW_ERR_RANGE;
    }
    st = get(at + 2u, buf, len);
    if (st != TFW_OK) {
        return st;
    }
    return tfw_suit_gate(p, &tr, digest, tr.image_len, buf, len);
}
