/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "boot_ota.h"

/* Erased flash. A short final page is padded with this so that the bytes the
 * device measures are the bytes an offline verifier measures: padding with zero
 * would make every image whose length is not a page multiple hash differently
 * on the device than in the signing tool, and the symptom would be a signature
 * that "randomly" fails for some builds. */
#define ERASED 0xFFu

static tfw_status_t flush(tfw_ota_t *o)
{
    uint32_t addr;
    tfw_status_t st;

    if (o->buf_used == 0u) {
        return TFW_OK;
    }
    /* The page this buffer belongs to is the one ending at `received`, because
     * bytes are only ever appended in order. Computing it from `received`
     * rather than keeping a separate cursor removes the one thing that could
     * disagree with itself. Offsets count from where the board stages an
     * image: the application region's base, or on a board with two slots the
     * secondary, which tfw-boot swaps in before the gate (boot_swap.h). */
    addr = (uint32_t)TFW_OTA_STAGE_DEST + (o->received - o->buf_used);

    while (o->buf_used < o->page_size) {
        o->buf[o->buf_used++] = ERASED;
    }
    st = o->write_page(addr, o->buf, o->page_size);
    o->buf_used = 0u;
    return st;
}

tfw_status_t tfw_ota_init(tfw_ota_t *o, tfw_ota_page_write_fn write_page,
                        uint16_t page_size)
{
    if (!write_page || page_size == 0u || page_size > TFW_OTA_PAGE_MAX) {
        return TFW_ERR_RANGE;
    }
    /* A power of two, because every address computation here assumes pages
     * tile the region exactly. A page size of, say, 100 would leave the last
     * page of a region straddling its end, and the write would land half in the
     * bootloader. */
    if ((page_size & (uint16_t)(page_size - 1u)) != 0u) {
        return TFW_ERR_RANGE;
    }

    memset(o, 0, sizeof *o);
    o->write_page = write_page;
    o->page_size  = page_size;
    o->state      = (uint8_t)TFW_OTA_IDLE;
    return TFW_OK;
}

tfw_status_t tfw_ota_begin(tfw_ota_t *o, uint32_t total_len)
{
    if (total_len == 0u || total_len > TFW_BOOT_APP_SIZE) {
        return TFW_ERR_RANGE;
    }
    /* Shorter than a trailer means nowhere to carry a signature. Refusing here
     * rather than at verify is what keeps the device from erasing a working
     * application to make room for something that could never have booted. */
    if (total_len <= (uint32_t)TFW_BOOT_TRAILER_LEN) {
        return TFW_ERR_RANGE;
    }
    if ((total_len % (uint32_t)o->page_size) != 0u) {
        return TFW_ERR_RANGE;
    }

    o->state        = (uint8_t)TFW_OTA_RECEIVING;
    o->declared_len = total_len;
    o->received     = 0u;
    o->next_offset  = 0u;
    o->buf_used     = 0u;
    return TFW_OK;
}

tfw_status_t tfw_ota_data(tfw_ota_t *o, uint32_t offset, const uint8_t *data,
                        uint8_t len)
{
    uint8_t i;

    if (o->state != (uint8_t)TFW_OTA_RECEIVING) {
        return TFW_ERR_BUSY;
    }
    if (len == 0u || !data) {
        return TFW_ERR_BAD_ARGC;
    }
    /* Exactly the expected offset. Not "at least", not "within a window": a
     * stream that may be rewound is a stream where the bytes finally measured
     * are not the bytes finally written, and the two would differ only for the
     * pages the rewind touched. */
    if (offset != o->next_offset) {
        return TFW_ERR_RANGE;
    }
    if ((uint32_t)len > (o->declared_len - o->received)) {
        return TFW_ERR_RANGE;
    }

    for (i = 0u; i < len; i++) {
        o->buf[o->buf_used++] = data[i];
        o->received++;
        if (o->buf_used == o->page_size) {
            tfw_status_t st = flush(o);
            if (st != TFW_OK) {
                o->state = (uint8_t)TFW_OTA_FAILED;
                return st;
            }
        }
    }
    o->next_offset = o->received;
    return TFW_OK;
}

tfw_status_t tfw_ota_end(tfw_ota_t *o)
{
    tfw_status_t st;

    if (o->state != (uint8_t)TFW_OTA_RECEIVING) {
        return TFW_ERR_BUSY;
    }
    /* Short is refused. An image whose tail never arrived would measure over
     * whatever the last page happened to hold, which is a measurement of
     * something nobody signed -- and the signature check would then fail with a
     * status that says "bad signature" rather than "truncated transfer". */
    if (o->received != o->declared_len) {
        o->state = (uint8_t)TFW_OTA_FAILED;
        return TFW_ERR_FRAMING;
    }

    st = flush(o);
    if (st != TFW_OK) {
        o->state = (uint8_t)TFW_OTA_FAILED;
        return st;
    }
    o->state = (uint8_t)TFW_OTA_COMPLETE;
    return TFW_OK;
}

void tfw_ota_abort(tfw_ota_t *o)
{
    o->state    = (uint8_t)TFW_OTA_IDLE;
    o->received = 0u;
    o->next_offset = 0u;
    o->buf_used = 0u;
    o->declared_len = 0u;
}

uint8_t tfw_ota_state(const tfw_ota_t *o)
{
    return o->state;
}

uint32_t tfw_ota_received(const tfw_ota_t *o)
{
    return o->received;
}
