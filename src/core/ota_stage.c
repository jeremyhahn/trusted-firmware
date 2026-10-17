/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application stages an OTA image in the external store -- issue #75,
 * docs/ota/adr-ota-image-format-and-trust.md decisions 6.2 and 6.3.
 *
 *   BEGIN  [total_len:4]  tfw-boot's bounds: more than a trailer, a whole
 *                         number of pages, and no more than the board's STAGE.
 *                         Erases the MARK sector first, so an interrupted
 *                         stream followed by a fresh BEGIN can never leave the
 *                         old record pointing at new bytes.
 *   DATA   [offset:4][bytes]  in order, no gaps, straight to the store. A
 *                         sector is erased as the stream first enters it.
 *   END                   reads STAGE back, checks the trailer at its end and
 *                         the SHA-256 over image_len bytes, then writes MARK.
 *   STATUS -> [state:1][received:4]
 *
 * WHAT END DOES NOT CHECK. The rollback floor, the signature and the manifest:
 * the floor and the pinned key live in the platform block, which the
 * application cannot read as tfw-boot does, and tfw-boot runs the whole gate
 * again before it installs anything. END refuses what is plainly not an image
 * for this board, so a bad transfer fails at the host rather than at reboot.
 *
 * WHY THE MARK IS IN THE STORE. The ADR has OTA_ACTIVATE set ota_state to
 * PENDING, but the platform block is tfw-boot's to write -- the application has
 * only tfw_hal_nvm_read. A record in the store is the staged mark tfw-boot
 * reads; who sets PENDING is OTA_ACTIVATE's, and its id is still a proposal.
 */
#include <string.h>
#include "tfw/boot.h"
#include "tfw/config.h"
#include "../crypto/sha256.h"
#include "ota_store.h"
#include "ota_stage.h"

#define BLOCK 32u

static uint8_t  state;
static uint32_t total;
static uint32_t received;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static tfw_status_t fail(tfw_status_t st)
{
    state = TFW_STAGE_FAILED;
    return st;
}

/* The trailer is the last TFW_BOOT_TRAILER_LEN bytes of the staged stream; the
 * measurement it carries is left in want for the MARK record. */
static tfw_status_t verify(uint8_t want[TFW_BOOT_MEASURE_LEN])
{
    uint8_t      blk[BLOCK];
    tfw_sha256_t c;
    uint32_t     tr = ((uint32_t)TFW_OTA_STORE_STAGE_ADDR) + total - TFW_BOOT_TRAILER_LEN;
    uint32_t     len;
    uint32_t     at;
    uint8_t      n;
    tfw_status_t st;

    st = tfw_store_read(tr, blk, TFW_BOOT_TR_MEASUREMENT);
    if (st != TFW_OK) {
        return st;
    }
    if (rd32(&blk[TFW_BOOT_TR_MAGIC]) != TFW_BOOT_TRAILER_MAGIC ||
        ((uint16_t)blk[TFW_BOOT_TR_VERSION] |
         ((uint16_t)blk[TFW_BOOT_TR_VERSION + 1u] << 8)) != TFW_BOOT_TRAILER_VERSION ||
        blk[TFW_BOOT_TR_BOARD_ID] != (uint8_t)TFW_BOARD_ID) {
        return TFW_ERR_UNSUPPORTED;
    }
    /* An image may not reach its own trailer -- boot/boot_image.c. */
    len = rd32(&blk[TFW_BOOT_TR_IMAGE_LEN]);
    if (len == 0u || len > total - TFW_BOOT_TRAILER_LEN) {
        return TFW_ERR_RANGE;
    }
    st = tfw_store_read(tr + TFW_BOOT_TR_MEASUREMENT, want, TFW_BOOT_MEASURE_LEN);
    if (st != TFW_OK) {
        return st;
    }

    tfw_sha256_init(&c);
    for (at = 0u; at < len; at += n) {
        n  = (len - at < BLOCK) ? (uint8_t)(len - at) : (uint8_t)BLOCK;
        st = tfw_store_read(((uint32_t)TFW_OTA_STORE_STAGE_ADDR) + at, blk, n);
        if (st != TFW_OK) {
            return st;
        }
        tfw_sha256_update(&c, blk, n);
    }
    tfw_sha256_final(&c, blk);
    if (memcmp(blk, want, TFW_BOOT_MEASURE_LEN) != 0) {
        return TFW_ERR_CRYPTO;
    }
    return TFW_OK;
}

static tfw_status_t mark(const uint8_t hash[TFW_BOOT_MEASURE_LEN])
{
    uint8_t      rec[TFW_STAGE_MARK_LEN];
    tfw_status_t st;

    wr32(rec, TFW_STAGE_MARK_MAGIC);
    wr32(&rec[TFW_STAGE_MARK_O_LEN], total);
    memcpy(&rec[TFW_STAGE_MARK_O_HASH], hash, TFW_BOOT_MEASURE_LEN);
    st = tfw_store_write(((uint32_t)TFW_OTA_STORE_MARK_ADDR) + TFW_STAGE_MARK_O_LEN,
                         &rec[TFW_STAGE_MARK_O_LEN],
                         (uint8_t)(TFW_STAGE_MARK_LEN - TFW_STAGE_MARK_O_LEN));
    if (st != TFW_OK) {
        return st;
    }
    return tfw_store_write(((uint32_t)TFW_OTA_STORE_MARK_ADDR), rec, TFW_STAGE_MARK_O_LEN);
}

void tfw_stage_init(void)
{
    (void)tfw_store_init();
}

tfw_status_t tfw_stage_cmd_begin(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t     len = tfw_read_u32(r);
    tfw_status_t st;

    (void)w;
    if (len <= (uint32_t)TFW_BOOT_TRAILER_LEN || len > ((uint32_t)TFW_OTA_STORE_STAGE_LEN) ||
        (len % (uint32_t)TFW_OTA_PAGE_MAX) != 0u) {
        return TFW_ERR_RANGE;
    }
    state    = TFW_STAGE_IDLE;
    total    = len;
    received = 0u;

    st = tfw_store_init();
    if (st == TFW_OK) {
        st = tfw_store_erase(((uint32_t)TFW_OTA_STORE_MARK_ADDR));
    }
    if (st == TFW_OK) {
        st = tfw_ota_prev_backup();   /* after the mark: no boot takes a stale image */
    }
    if (st != TFW_OK) {
        return fail(st);
    }
    state = TFW_STAGE_RECEIVING;
    return TFW_OK;
}

tfw_status_t tfw_stage_cmd_data(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t       off = tfw_read_u32(r);
    uint8_t        n   = tfw_reader_remaining(r);
    const uint8_t *p   = tfw_read_blob(r, n);
    uint32_t       s;
    tfw_status_t   st;

    (void)w;
    if (state != TFW_STAGE_RECEIVING) {
        return TFW_ERR_BUSY;
    }
    if (off != received || (uint32_t)n > total - received) {
        return TFW_ERR_RANGE;
    }
    /* Every sector boundary the stream crosses or starts on is erased once, on
     * the way in: the order is fixed, so received says which are done. */
    for (s = (received + ((uint32_t)TFW_STORE_SECTOR) - 1u) & ~(((uint32_t)TFW_STORE_SECTOR) - 1u);
         s < received + n; s += ((uint32_t)TFW_STORE_SECTOR)) {
        st = tfw_store_erase(((uint32_t)TFW_OTA_STORE_STAGE_ADDR) + s);
        if (st != TFW_OK) {
            return fail(st);
        }
    }
    st = tfw_store_write(((uint32_t)TFW_OTA_STORE_STAGE_ADDR) + received, p, n);
    if (st != TFW_OK) {
        return fail(st);
    }
    received += n;
    return TFW_OK;
}

tfw_status_t tfw_stage_cmd_end(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t      hash[TFW_BOOT_MEASURE_LEN];
    tfw_status_t st;

    (void)r;
    (void)w;
    if (state != TFW_STAGE_RECEIVING) {
        return TFW_ERR_BUSY;
    }
    if (received != total) {
        return fail(TFW_ERR_FRAMING);
    }
    st = verify(hash);
    if (st == TFW_OK) {
        st = mark(hash);
    }
    if (st != TFW_OK) {
        return fail(st);
    }
    state = TFW_STAGE_COMPLETE;
    return TFW_OK;
}

tfw_status_t tfw_stage_cmd_status(tfw_reader_t *r, tfw_writer_t *w)
{
    (void)r;
    tfw_write_u8(w, state);
    tfw_write_u32(w, received);
    return TFW_OK;
}
