/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/rmt_rx.h: the RX channel
 * calls src/hal/esp32/hal_pulse_rmt.c makes, with the types and field names of
 * components/esp_driver_rmt/include/driver/rmt_rx.h, rmt_common.h and
 * rmt_types.h, and of components/hal/include/hal/rmt_types.h for the symbol.
 * test/mock/esp_rmt_mock.c answers them; its controls are esp_rmt_mock.h.
 */
#ifndef TFW_MOCK_DRIVER_RMT_RX_H
#define TFW_MOCK_DRIVER_RMT_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef int gpio_num_t;

typedef enum {
    RMT_CLK_SRC_DEFAULT = 0
} rmt_clock_source_t;

/* One symbol: two levels, each with its duration in ticks. A duration of 0
 * is the end marker the receiver writes when the line goes idle. */
typedef union {
    struct {
        uint16_t duration0 : 15;
        uint16_t level0    : 1;
        uint16_t duration1 : 15;
        uint16_t level1    : 1;
    };
    uint32_t val;
} rmt_symbol_word_t;

typedef struct rmt_channel_t *rmt_channel_handle_t;

typedef struct {
    gpio_num_t         gpio_num;
    rmt_clock_source_t clk_src;
    uint32_t           resolution_hz;
    size_t             mem_block_symbols;
    int                intr_priority;
    struct {
        uint32_t invert_in    : 1;
        uint32_t with_dma     : 1;
        uint32_t io_loop_back : 1;
        uint32_t allow_pd     : 1;
    } flags;
} rmt_rx_channel_config_t;

typedef struct {
    uint32_t signal_range_min_ns;
    uint32_t signal_range_max_ns;
    struct extra_rmt_receive_flags {
        uint32_t en_partial_rx : 1;
    } flags;
} rmt_receive_config_t;

typedef struct {
    rmt_symbol_word_t *received_symbols;
    size_t             num_symbols;
    struct {
        uint32_t is_last : 1;
    } flags;
} rmt_rx_done_event_data_t;

typedef bool (*rmt_rx_done_callback_t)(rmt_channel_handle_t rx_chan,
                                       const rmt_rx_done_event_data_t *edata,
                                       void *user_ctx);

typedef struct {
    rmt_rx_done_callback_t on_recv_done;
} rmt_rx_event_callbacks_t;

esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config,
                             rmt_channel_handle_t *ret_chan);
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t rx_channel,
                                          const rmt_rx_event_callbacks_t *cbs,
                                          void *user_data);
esp_err_t rmt_receive(rmt_channel_handle_t rx_channel, void *buffer,
                      size_t buffer_size, const rmt_receive_config_t *config);
esp_err_t rmt_enable(rmt_channel_handle_t channel);
esp_err_t rmt_disable(rmt_channel_handle_t channel);
esp_err_t rmt_del_channel(rmt_channel_handle_t channel);

#endif
