/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The controls of test/mock/esp_rmt_mock.c: one RMT RX channel, the frame it
 * receives, and a failure to inject at each call.
 *
 * The frame is a list of chunks, handed to the receive callback the way the
 * driver's ISR hands them: copied into the caller's buffer first, one chunk
 * per esp_timer_get_time read once rmt_receive has armed the channel, the
 * last with is_last set. A chunk larger than the caller's buffer is counted
 * in `overflow` and not delivered. The clock advances `step` microseconds per
 * read. Like ESP-IDF, the mock refuses a call out of the channel's order --
 * receive before enable, delete while enabled, any call on a channel deleted
 * -- and counts it in `misuse`, which must stay 0.
 */
#ifndef TFW_MOCK_ESP_RMT_MOCK_H
#define TFW_MOCK_ESP_RMT_MOCK_H

#include <stddef.h>
#include <stdint.h>
#include "driver/rmt_rx.h"

#define MOCK_RMT_CHUNKS 4u

typedef struct {
    const rmt_symbol_word_t *symbols;
    size_t                   n;
    uint8_t                  is_last;
} mock_rmt_chunk_t;

typedef struct {
    rmt_rx_channel_config_t cfg;          /* the last channel's */
    rmt_receive_config_t    rcv;          /* the last receive's */
    size_t                  buffer_size;  /* the last receive's buffer, bytes */
    mock_rmt_chunk_t        chunk[MOCK_RMT_CHUNKS];
    size_t                  chunks;       /* scripted */
    size_t                  delivered;
    esp_err_t               new_err;      /* returned instead, when set */
    esp_err_t               cb_err;
    esp_err_t               enable_err;
    esp_err_t               receive_err;
    int64_t                 now;          /* esp_timer_get_time's clock */
    int64_t                 step;
    unsigned                created, deleted, misuse, overflow;
    uint8_t                 open;         /* a channel is held */
    uint8_t                 enabled;
    uint8_t                 armed;        /* rmt_receive ran, no frame end yet */
} mock_rmt_t;

extern mock_rmt_t mock_rmt;

/* No channel, no chunk, nothing injected; the clock at 0, stepping 100 us. */
void mock_rmt_reset(void);

/* Appends a chunk of the frame. */
void mock_rmt_chunk(const rmt_symbol_word_t *symbols, size_t n, uint8_t is_last);

#endif
