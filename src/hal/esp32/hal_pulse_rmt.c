/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Pulse timing on the RMT receiver (#48, ADR follow-up 8).
 *
 * The RMT times every edge in hardware, so an interval no longer reads long
 * by the length of a preemption, as hal_pulse.c's poll can. The contract is
 * tfw/hal.h's and does not change; what the receiver brings to it:
 *
 *  - Intervals run edge to edge. The receiver records from the first edge
 *    after it is armed, so the level the pin already holds when the call
 *    begins is never an interval; the poll counted its tail as the first.
 *  - A level held longer than timeout_us ends the capture: it is the
 *    receiver's idle threshold, as it is the AVR's per-interval timeout. The
 *    whole call is bounded as the AVR's is: timeout_us for the first edge,
 *    then for each interval and for the quiet line that ends the last.
 *  - Ticks are 1 us up to a timeout of 32767 us and 2 us above it, because
 *    the idle threshold is a 15-bit tick count.
 *  - A pulse shorter than RMT_GLITCH_NS is filtered out as noise.
 *  - The channel is claimed for the call and given back after it; no free
 *    channel is TFW_ERR_BUSY. While claimed, the driver holds the pin as an
 *    input with its pull-up.
 *
 * The receive runs in partial mode, so a frame longer than one buffer reaches
 * the callback in pieces and a signal that never goes quiet still ends at
 * max_edges. Partial mode needs the RX ping-pong of the esp32c3, -s3, -c6 and
 * -p4; the classic esp32 has none, rmt_receive refuses it there, and a board
 * on that part keeps the poll (mk/arch/esp32.mk, ESP_PULSE).
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/rmt_rx.h"
#include "esp_timer.h"
#include "soc/soc_caps.h"

#include "tfw/hal.h"

#define RMT_IDLE_TICKS_MAX 32767u   /* the idle threshold's 15 bits */
#define RMT_GLITCH_NS      1000u

typedef struct {
    uint16_t         *out;
    uint8_t           max;
    uint8_t           n;
    uint8_t           level;        /* the level the first interval holds */
    uint8_t           started;
    uint8_t           tick_us;
    volatile uint8_t  done;
} capture_t;

/* One level the receiver recorded: skipped until the first at the start
 * level, an interval from there. Returns 1 when the capture is complete. */
static int take(capture_t *c, unsigned level, unsigned ticks)
{
    uint32_t us;

    if (ticks == 0u) {
        return 1;                   /* the end marker: the line went quiet */
    }
    if (!c->started) {
        if (level != c->level) {
            return 0;
        }
        c->started = 1u;
    }
    us = (uint32_t)ticks * c->tick_us;
    c->out[c->n++] = (uint16_t)(us > 0xFFFFu ? 0xFFFFu : us);
    return c->n >= c->max;
}

/* The driver's ISR: a buffer of symbols, the last one once the line is idle. */
static bool on_recv(rmt_channel_handle_t chan, const rmt_rx_done_event_data_t *ev,
                    void *ctx)
{
    capture_t *c = ctx;
    size_t     i;

    (void)chan;
    for (i = 0u; i < ev->num_symbols && !c->done; i++) {
        const rmt_symbol_word_t *s = &ev->received_symbols[i];

        if (take(c, s->level0, s->duration0) || take(c, s->level1, s->duration1)) {
            c->done = 1u;
        }
    }
    if (ev->flags.is_last) {
        c->done = 1u;
    }
    return false;
}

static tfw_status_t status_of(esp_err_t err)
{
    if (err == ESP_ERR_NOT_FOUND) {
        return TFW_ERR_BUSY;        /* every RX channel is taken */
    }
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return TFW_ERR_UNSUPPORTED; /* no RX ping-pong on this part */
    }
    return TFW_ERR_INTERNAL;
}

tfw_status_t tfw_hal_pulse_capture(const tfw_pinref_t *p, uint8_t start_level,
                                  uint16_t *out_us, uint8_t max_edges,
                                  uint32_t timeout_us, uint8_t *out_n)
{
    rmt_symbol_word_t        buf[SOC_RMT_MEM_WORDS_PER_CHANNEL];
    uint8_t                  tick_us = timeout_us > RMT_IDLE_TICKS_MAX ? 2u : 1u;
    uint32_t                 idle_us = timeout_us > 2u * RMT_IDLE_TICKS_MAX
                                     ? 2u * RMT_IDLE_TICKS_MAX : timeout_us;
    rmt_rx_channel_config_t  cfg = {
        .gpio_num          = (gpio_num_t)p->gpio,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 1000000u / tick_us,
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
    };
    rmt_receive_config_t     rcv = {
        .signal_range_min_ns = RMT_GLITCH_NS,
        .signal_range_max_ns = idle_us * 1000u,
        .flags.en_partial_rx = 1u,
    };
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = on_recv };
    capture_t                c   = {
        .out = out_us, .max = max_edges, .level = start_level ? 1u : 0u,
        .tick_us = tick_us,
    };
    rmt_channel_handle_t     chan = NULL;
    esp_err_t                err;

    *out_n = 0u;
    if (max_edges == 0u) {
        return TFW_ERR_TIMEOUT;
    }
    err = rmt_new_rx_channel(&cfg, &chan);
    if (err != ESP_OK) {
        return status_of(err);
    }
    err = rmt_rx_register_event_callbacks(chan, &cbs, &c);
    if (err == ESP_OK) {
        err = rmt_enable(chan);
        if (err == ESP_OK) {
            err = rmt_receive(chan, buf, sizeof buf, &rcv);
            if (err == ESP_OK) {
                int64_t deadline = esp_timer_get_time()
                                 + (int64_t)timeout_us * ((int64_t)max_edges + 2);

                while (!c.done && esp_timer_get_time() < deadline) {
                }
            }
            /* The ISR runs on this core and is off once this returns, so
             * the capture below is no longer written. */
            (void)rmt_disable(chan);
        }
    }
    (void)rmt_del_channel(chan);
    if (err != ESP_OK) {
        return status_of(err);
    }
    *out_n = c.n;
    return c.n ? TFW_OK : TFW_ERR_TIMEOUT;
}

tfw_status_t tfw_hal_pulse_in(const tfw_pinref_t *p, uint8_t level,
                             uint32_t timeout_us, uint32_t *out_us)
{
    uint16_t     us = 0u;
    uint8_t      n;
    tfw_status_t st = tfw_hal_pulse_capture(p, level, &us, 1u, timeout_us, &n);

    if (st == TFW_OK) {
        *out_us = us;
    }
    return st;
}
