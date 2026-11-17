/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * SHA-256 on the STM32H5's HASH peripheral (#544), in the order of ST's
 * HAL's polling path: configure and INIT, NBLW, the message a word at a time,
 * then DCAL and wait for DCIS. Register facts: h5_crypto_regs.h.
 */
#include "h5_hash.h"

#include "h5_crypto_regs.h"

/* Polls SR until (SR & mask) == want, at most TFW_H5_POLL_LIMIT reads. */
static int h5_hash_wait(uintptr_t base, uint32_t mask, uint32_t want)
{
    for (uint32_t i = 0; i < TFW_H5_POLL_LIMIT; i++) {
        if ((tfw_h5_rd(base + TFW_H5_HASH_SR) & mask) == want)
            return 1;
    }
    return 0;
}

tfw_status_t tfw_h5_hash_sha256(uintptr_t base, const uint8_t *data, uint16_t len,
                                uint8_t out[32])
{
    /* The valid bits of the last word; 0 when it is whole (or there is none). */
    uint32_t nblw = (uint32_t)(len % 4u) * 8u;

    tfw_h5_wr(base + TFW_H5_HASH_CR,
              TFW_H5_HASH_CR_ALGO_SHA256 | TFW_H5_HASH_CR_BYTE_SWAP | TFW_H5_HASH_CR_INIT);
    /* The DCIS of an earlier digest (or of one that timed out and landed
     * late) would pass this digest's poll while HR still holds the earlier
     * one: clear it (rc_w0, as HAL __HAL_HASH_CLEAR_FLAG does) after INIT,
     * which drops a digest still in flight. */
    tfw_h5_wr(base + TFW_H5_HASH_SR, 0u);
    tfw_h5_wr(base + TFW_H5_HASH_STR, nblw);

    uint32_t words = 0;
    for (uint32_t i = 0; i < len; i += 4u) {
        uint32_t w = 0;
        for (uint32_t b = 0; b < 4u && i + b < len; b++)
            w |= (uint32_t)data[i + b] << (8u * b);
        tfw_h5_wr(base + TFW_H5_HASH_DIN, w);
        if (++words % TFW_H5_HASH_BLOCK_WORDS == 0u &&
            !h5_hash_wait(base, TFW_H5_HASH_SR_BUSY, 0u))
            return TFW_ERR_TIMEOUT;
    }

    tfw_h5_wr(base + TFW_H5_HASH_STR, nblw | TFW_H5_HASH_STR_DCAL);
    if (!h5_hash_wait(base, TFW_H5_HASH_SR_DCIS, TFW_H5_HASH_SR_DCIS))
        return TFW_ERR_TIMEOUT;

    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t h = tfw_h5_rd(base + TFW_H5_HASH_HR + 4u * i);
        out[4u * i + 0u] = (uint8_t)(h >> 24);
        out[4u * i + 1u] = (uint8_t)(h >> 16);
        out[4u * i + 2u] = (uint8_t)(h >> 8);
        out[4u * i + 3u] = (uint8_t)h;
    }
    return TFW_OK;
}
