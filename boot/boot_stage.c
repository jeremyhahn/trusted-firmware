/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's side of the external OTA store: read the stager's MARK, check the
 * staged image again, copy it into the application region -- see boot_stage.h.
 *
 * The store is only ever read. The NOR read path is here and not shared with
 * src/store/spi_nor.c: that driver reaches the bus through the application
 * HAL's pin table, which tfw-boot does not link, and it carries the program and
 * erase path a bootloader has no use for. Three opcodes are all this needs.
 */
#include <string.h>

#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_stage.h"
#include "boot_store.h"
#include "boot_trial.h"
#include "boot_verify.h"

#define OP_PROGRAM 0x02u
#define OP_READ    0x03u
#define OP_STATUS  0x05u
#define OP_WREN    0x06u
#define OP_JEDEC   0x9Fu
#define ST_WIP     0x01u

/* Status reads before a part still busy with the application's last program
 * or erase is given up on: a watchdog reset can land mid-erase, and a 4 KB
 * erase is tens of milliseconds. Bounded, because tfw-boot has no clock. */
#define BUSY_POLLS 0xFFFFu

#define STAGE ((uint32_t)TFW_OTA_STORE_STAGE_ADDR)

/* One page of the application region on its way in, and before that the
 * installed trailer compared against the staged one. */
static uint8_t page[TFW_OTA_PAGE_MAX];

static uint32_t rd32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

/* A part that is not mid-write: status reads only, the one command a busy part
 * takes. */
static tfw_status_t idle(void)
{
    uint8_t      op = OP_STATUS;
    uint8_t      sr;
    uint16_t     n;
    tfw_status_t st = TFW_OK;

    for (n = 0u; st == TFW_OK; n++) {
        if (n == BUSY_POLLS) {
            return TFW_ERR_TIMEOUT;
        }
        st = tfw_boot_store_cmd(&op, 1u, &sr, 1u);
        if (st == TFW_OK && (sr & ST_WIP) == 0u) {
            break;
        }
    }
    return st;
}

/* A part that answers, and is not mid-write. An absent part reads all ones or
 * all zeros, depending on what MISO floats to. */
static tfw_status_t ready(void)
{
    uint8_t      op = OP_JEDEC;
    uint8_t      id[3];
    tfw_status_t st = tfw_boot_store_cmd(&op, 1u, id, 3u);

    if (st == TFW_OK && ((id[0] == 0x00u && id[1] == 0x00u) || (id[0] == 0xFFu && id[1] == 0xFFu))) {
        st = TFW_ERR_NACK;
    }
    if (st == TFW_OK) {
        st = idle();
    }
    return st;
}

/* Every read of the store, of any length, in one select. */
static tfw_status_t fetch(uint32_t addr, uint8_t *buf, uint16_t len)
{
    uint8_t cmd[4];

    cmd[0] = OP_READ;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)addr;
    return tfw_boot_store_cmd(cmd, 4u, buf, len);
}

/* The reader tfw_boot_measure_from takes. */
static tfw_status_t store_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    return fetch(addr, buf, len);
}

/* One page of the application region from the staged stream. The stream is
 * the image's `body` bytes followed by its trailer; the region holds the body
 * from its base and the trailer at its end, which on the 2560 is far past the
 * end of STAGE. What lies between is erased. */
static tfw_status_t copy_page(uint32_t src, uint32_t a, uint16_t ps, uint32_t body)
{
    uint32_t     rel = a - TFW_BOOT_APP_BASE;
    uint32_t     end = a + ps;
    tfw_status_t st  = TFW_OK;

    memset(page, 0xFF, ps);
    if (rel < body && a < TFW_BOOT_TRAILER_ADDR) {
        uint32_t stop = TFW_BOOT_APP_BASE + body;

        if (stop > end) {
            stop = end;
        }
        st = fetch(src + rel, page, (uint16_t)(stop - a));
    }
    if (st == TFW_OK && end > TFW_BOOT_TRAILER_ADDR) {
        uint32_t from = (a > TFW_BOOT_TRAILER_ADDR) ? a : TFW_BOOT_TRAILER_ADDR;

        st = fetch(src + body + (from - TFW_BOOT_TRAILER_ADDR),
                   &page[from - a], (uint16_t)(end - from));
    }
    if (st == TFW_OK) {
        st = tfw_boot_flash_write_page(a - TFW_BOOT_APP_BASE + (uint32_t)TFW_OTA_STAGE_DEST,
                                       page);
    }
    return st;
}

/* 1 when the slot whose trailer is at `at` already holds the staged image. */
static uint8_t holds(uint32_t at, const uint8_t *raw)
{
    tfw_boot_flash_read(at, page, TFW_BOOT_TRAILER_LEN);
    return (uint8_t)(memcmp(page, raw, TFW_BOOT_TRAILER_LEN) == 0);
}

/* The body's pages in order, then the page that holds the trailer, last: until
 * it is written the region has no trailer for this image, so a cut anywhere
 * before it cannot leave something that parses as the new image. */
static tfw_status_t copy(uint32_t src, uint32_t body)
{
    uint16_t     ps = tfw_boot_flash_page_size();
    uint32_t     tp;
    uint32_t     a;
    tfw_status_t st = TFW_OK;

    if (ps == 0u || ps > TFW_OTA_PAGE_MAX) {
        return TFW_ERR_RANGE;
    }
    tp = TFW_BOOT_TRAILER_ADDR & ~(uint32_t)(ps - 1u);
    for (a = TFW_BOOT_APP_BASE; st == TFW_OK && a < tp && a - TFW_BOOT_APP_BASE < body; a += ps) {
        st = copy_page(src, a, ps, body);
    }
    for (a = tp; st == TFW_OK && a < TFW_BOOT_APP_END; a += ps) {
        st = copy_page(src, a, ps, body);
    }
    return st;
}

/* The row key's signature over OTA_ACTIVATE's Sig_structure (#137,
 * tfw_boot_cose_activate), stored beside the MARK by the
 * application. TFW_ERR_AUTH_REQUIRED for a missing or bad one. The element
 * keeps its key, as tfw_boot_evaluate's check finds. */
static tfw_status_t authorized(const uint8_t *hash, uint32_t version)
{
    uint8_t        sig[TFW_BOOT_SIG_LEN + 1u];
    uint8_t        key[TFW_BOOT_PUBKEY_LEN];
    const uint8_t *k = key;
    uint8_t        len;
    tfw_status_t   st;

    st = store_read((uint32_t)TFW_OTA_STORE_MARK_ADDR + TFW_STAGE_MARK_O_SIG, sig, (uint8_t)sizeof sig);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_boot_pubkey(key);
    if (st == TFW_ERR_UNSUPPORTED) {
        k = 0;
    } else if (st != TFW_OK) {
        return st;
    }
    /* page is free here: holds() is done with it and copy() not begun. */
    len = tfw_boot_cose_activate(page, hash, version, sig[TFW_BOOT_SIG_LEN]);
    if (tfw_boot_verify(page, len, sig, k) != TFW_OK) {
        return TFW_ERR_AUTH_REQUIRED;
    }
    return TFW_OK;
}

static tfw_status_t take(tfw_boot_platform_t *p, uint32_t floor)
{
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    uint8_t            mark[TFW_STAGE_MARK_LEN];
    uint8_t            digest[TFW_BOOT_MEASURE_LEN];
    tfw_boot_trailer_t tr;
    uint32_t           total;
    uint32_t           body;
    tfw_status_t       st;

    st = ready();
    if (st == TFW_OK) {
        st = store_read((uint32_t)TFW_OTA_STORE_MARK_ADDR, mark, TFW_STAGE_MARK_LEN);
    }
    if (st != TFW_OK) {
        return st;
    }
    total = rd32(&mark[TFW_STAGE_MARK_O_LEN]);
    if (rd32(mark) != TFW_STAGE_MARK_MAGIC) {
        return TFW_ERR_BAD_STATE;
    }
    if (total <= TFW_BOOT_TRAILER_LEN || total > (uint32_t)TFW_OTA_STORE_STAGE_LEN ||
        total > TFW_BOOT_APP_SIZE) {
        return TFW_ERR_RANGE;
    }
    body = total - TFW_BOOT_TRAILER_LEN;
    st = store_read(STAGE + body, raw, TFW_BOOT_TRAILER_LEN);
    if (st != TFW_OK) {
        return st;
    }

    /* The image the store holds is the one already installed: the mark stays
     * until the application's next OTA_BEGIN erases it, and every boot in
     * between finds it. Unless a copy of it was cut short. Where a staged
     * image is copied into a second slot, that slot is checked too: after a
     * fall-back it holds the image that failed, which gets no second trial. */
    if (p->boot_state != TFW_BOOT_STATE_STAGED &&
        (holds(TFW_BOOT_TRAILER_ADDR, raw) != 0u ||
         holds(TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE + (uint32_t)TFW_OTA_STAGE_DEST,
               raw) != 0u)) {
        return TFW_ERR_BAD_STATE;
    }

    /* The gate's own checks, before a byte of the running image is replaced:
     * an image tfw_boot_evaluate would refuse for its board or its version is
     * not worth destroying a good one for. */
    st = tfw_boot_trailer_parse(raw, &tr);
    if (st == TFW_OK) {
        st = tfw_boot_binding_permit(&tr, TFW_BOARD_ID);
    }
    if (st == TFW_OK) {
        st = tfw_boot_rollback_permit(tr.image_version, floor);
    }
    if (st == TFW_OK && tr.image_len > body) {
        st = TFW_ERR_RANGE;
    }
    if (st == TFW_OK) {
        st = tfw_boot_measure_from(store_read, STAGE, tr.image_len, digest);
    }
    if (st == TFW_OK &&
        (memcmp(digest, tr.measurement, TFW_BOOT_MEASURE_LEN) != 0 ||
         memcmp(digest, &mark[TFW_STAGE_MARK_O_HASH], TFW_BOOT_MEASURE_LEN) != 0)) {
        st = TFW_ERR_CRYPTO;
    }
    if (st == TFW_OK) {
        st = authorized(digest, tr.image_version);
    }
    if (st != TFW_OK) {
        return st;
    }

    p->boot_state = TFW_BOOT_STATE_STAGED;
    tfw_boot_platform_store(p);
    st = copy(STAGE, body);
    if (st != TFW_OK) {
        return st;   /* STAGED stays: the next boot copies again */
    }
    p->boot_state = TFW_BOOT_STATE_GOOD;
    p->ota_state  = TFW_BOOT_OTA_PENDING;
    tfw_boot_platform_store(p);
    return TFW_OK;
}

tfw_status_t tfw_boot_stage_take(tfw_boot_platform_t *p, uint32_t floor)
{
    tfw_status_t st = TFW_OK;

    /* A copy a reset cut short is resumed whatever else holds; a fresh image
     * only where a new round may start -- never over one waiting for, or
     * having passed, its trial. */
    if (p->boot_state != TFW_BOOT_STATE_STAGED) {
        st = tfw_boot_trial_begin_permit(p);
    }
    if (st == TFW_OK) {
        st = tfw_boot_store_open();
        if (st == TFW_OK) {
            st = take(p, floor);
            tfw_boot_store_close();
        }
    }
    /* An image its row key did not authorize is never offered again (#137). */
    if (st == TFW_ERR_AUTH_REQUIRED) {
        (void)tfw_boot_stage_forget();
    }
    return st;
}

tfw_status_t tfw_boot_stage_forget(void)
{
    uint8_t      cmd[4u + 4u];
    uint32_t     a = (uint32_t)TFW_OTA_STORE_MARK_ADDR;
    tfw_status_t st = tfw_boot_store_open();

    /* The magic, the MARK's first word, programmed to zero: no erase, and a
     * program a reset cuts short has already cleared a bit of it. */
    if (st == TFW_OK) {
        st = ready();
    }
    if (st == TFW_OK) {
        cmd[0] = OP_WREN;
        st = tfw_boot_store_cmd(cmd, 1u, cmd, 0u);
    }
    if (st == TFW_OK) {
        cmd[0] = OP_PROGRAM;
        cmd[1] = (uint8_t)(a >> 16);
        cmd[2] = (uint8_t)(a >> 8);
        cmd[3] = (uint8_t)a;
        memset(&cmd[4], 0, 4u);
        st = tfw_boot_store_cmd(cmd, (uint8_t)sizeof cmd, cmd, 0u);
    }
    /* Busy with the program: only a status read until it ends. */
    if (st == TFW_OK) {
        st = idle();
    }
    tfw_boot_store_close();
    return st;
}

void tfw_boot_stage_after_reset(tfw_boot_reset_action_t trial, tfw_boot_reset_action_t act)
{
    if (trial == TFW_BOOT_RESET_FAIL && act != TFW_BOOT_RESET_SETTLE) {
        (void)tfw_boot_stage_forget();
    }
}

tfw_status_t tfw_boot_stage_copy(uint32_t src, uint32_t body)
{
    tfw_status_t st = tfw_boot_store_open();

    if (st == TFW_OK) {
        st = ready();
        if (st == TFW_OK) {
            st = copy(src, body);
        }
        tfw_boot_store_close();
    }
    return st;
}
