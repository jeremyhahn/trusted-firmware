/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A host RMT receiver and microsecond clock for src/hal/esp32/hal_pulse_rmt.c.
 * The contract is test/mock/esp32/esp_rmt_mock.h's.
 */
#include <string.h>
#include "esp_rmt_mock.h"
#include "esp_timer.h"

struct rmt_channel_t {
    uint8_t id;
};

mock_rmt_t mock_rmt;

static struct rmt_channel_t      the_channel;
static rmt_rx_done_callback_t    the_cb;
static void                     *the_ctx;
static void                     *the_buf;

void mock_rmt_reset(void)
{
    memset(&mock_rmt, 0, sizeof mock_rmt);
    mock_rmt.step = 100;
    the_cb  = NULL;
    the_ctx = NULL;
    the_buf = NULL;
}

void mock_rmt_chunk(const rmt_symbol_word_t *symbols, size_t n, uint8_t is_last)
{
    if (mock_rmt.chunks < MOCK_RMT_CHUNKS) {
        mock_rmt.chunk[mock_rmt.chunks].symbols = symbols;
        mock_rmt.chunk[mock_rmt.chunks].n       = n;
        mock_rmt.chunk[mock_rmt.chunks].is_last = is_last;
        mock_rmt.chunks++;
    }
}

static int is_ours(rmt_channel_handle_t chan)
{
    if (chan != &the_channel || !mock_rmt.open) {
        mock_rmt.misuse++;
        return 0;
    }
    return 1;
}

/* What the ISR does for one chunk: copy, then call back. */
static void deliver(void)
{
    const mock_rmt_chunk_t  *c = &mock_rmt.chunk[mock_rmt.delivered++];
    rmt_rx_done_event_data_t ev;

    if (c->n * sizeof(rmt_symbol_word_t) > mock_rmt.buffer_size) {
        mock_rmt.overflow++;
        return;
    }
    memcpy(the_buf, c->symbols, c->n * sizeof(rmt_symbol_word_t));
    memset(&ev, 0, sizeof ev);
    ev.received_symbols = the_buf;
    ev.num_symbols      = c->n;
    ev.flags.is_last    = c->is_last ? 1u : 0u;
    if (c->is_last) {
        mock_rmt.armed = 0u;
    }
    if (the_cb) {
        (void)the_cb(&the_channel, &ev, the_ctx);
    }
}

int64_t esp_timer_get_time(void)
{
    int64_t t = mock_rmt.now;

    mock_rmt.now += mock_rmt.step;
    if (mock_rmt.armed && mock_rmt.enabled && mock_rmt.delivered < mock_rmt.chunks) {
        deliver();
    }
    return t;
}

esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config,
                             rmt_channel_handle_t *ret_chan)
{
    if (mock_rmt.new_err) {
        return mock_rmt.new_err;
    }
    if (mock_rmt.open) {
        return ESP_ERR_NOT_FOUND;       /* the one channel is taken */
    }
    mock_rmt.cfg  = *config;
    mock_rmt.open = 1u;
    mock_rmt.created++;
    *ret_chan = &the_channel;
    return ESP_OK;
}

esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t rx_channel,
                                          const rmt_rx_event_callbacks_t *cbs,
                                          void *user_data)
{
    if (!is_ours(rx_channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_rmt.enabled) {
        mock_rmt.misuse++;
        return ESP_ERR_INVALID_STATE;
    }
    if (mock_rmt.cb_err) {
        return mock_rmt.cb_err;
    }
    the_cb  = cbs->on_recv_done;
    the_ctx = user_data;
    return ESP_OK;
}

esp_err_t rmt_enable(rmt_channel_handle_t channel)
{
    if (!is_ours(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_rmt.enabled) {
        mock_rmt.misuse++;
        return ESP_ERR_INVALID_STATE;
    }
    if (mock_rmt.enable_err) {
        return mock_rmt.enable_err;
    }
    mock_rmt.enabled = 1u;
    return ESP_OK;
}

esp_err_t rmt_receive(rmt_channel_handle_t rx_channel, void *buffer,
                      size_t buffer_size, const rmt_receive_config_t *config)
{
    if (!is_ours(rx_channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mock_rmt.enabled || mock_rmt.armed) {
        mock_rmt.misuse++;
        return ESP_ERR_INVALID_STATE;
    }
    if (((uintptr_t)buffer & 3u) != 0u || (buffer_size & 3u) != 0u) {
        mock_rmt.misuse++;              /* the driver's alignment check */
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_rmt.receive_err) {
        return mock_rmt.receive_err;
    }
    mock_rmt.rcv         = *config;
    mock_rmt.buffer_size = buffer_size;
    mock_rmt.armed       = 1u;
    the_buf              = buffer;
    return ESP_OK;
}

esp_err_t rmt_disable(rmt_channel_handle_t channel)
{
    if (!is_ours(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mock_rmt.enabled) {
        mock_rmt.misuse++;
        return ESP_ERR_INVALID_STATE;
    }
    mock_rmt.enabled = 0u;
    mock_rmt.armed   = 0u;
    return ESP_OK;
}

esp_err_t rmt_del_channel(rmt_channel_handle_t channel)
{
    if (!is_ours(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_rmt.enabled) {
        mock_rmt.misuse++;              /* the driver deletes only in its init state */
        return ESP_ERR_INVALID_STATE;
    }
    mock_rmt.open = 0u;
    mock_rmt.deleted++;
    the_cb  = NULL;
    the_ctx = NULL;
    return ESP_OK;
}
