/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 pulse timing on the RMT receiver (#48, ADR follow-up 8):
 * src/hal/esp32/hal_pulse_rmt.c against test/mock/esp_rmt_mock.c. A frame is
 * scripted as the receiver records it -- symbols of two levels with their
 * tick counts, a zero count where the line went quiet -- and the intervals
 * are what PULSE_CAPTURE and PULSE_IN answer. Every case also asserts that
 * the channel was given back, and that no driver call came out of order.
 */
#include "unity.h"
#include "tfw/hal.h"
#include "esp_rmt_mock.h"
#include "soc/soc_caps.h"

#define SYM(l0, d0, l1, d1) \
    { { .duration0 = (d0), .level0 = (l0), .duration1 = (d1), .level1 = (l1) } }

static const tfw_pinref_t pin = { 21u };

static uint16_t iv[31];
static uint8_t  n;

void setUp(void)
{
    mock_rmt_reset();
    for (n = 0u; n < 31u; n++) {
        iv[n] = 0xA5A5u;
    }
    n = 0xFFu;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, mock_rmt.misuse);
    TEST_ASSERT_EQUAL_UINT(0u, mock_rmt.overflow);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_rmt.open);
    TEST_ASSERT_EQUAL_UINT(mock_rmt.created, mock_rmt.deleted);
}

/* The channel and the receive as the driver is asked for them. */
static void test_config(void)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 80, 0, 0) };

    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_INT(21, mock_rmt.cfg.gpio_num);
    TEST_ASSERT_EQUAL_UINT32(1000000u, mock_rmt.cfg.resolution_hz);
    TEST_ASSERT_EQUAL_UINT(SOC_RMT_MEM_WORDS_PER_CHANNEL, mock_rmt.cfg.mem_block_symbols);
    TEST_ASSERT_EQUAL_UINT(0u, mock_rmt.cfg.flags.with_dma);
    TEST_ASSERT_EQUAL_UINT32(1000u, mock_rmt.rcv.signal_range_min_ns);
    TEST_ASSERT_EQUAL_UINT32(1000000u, mock_rmt.rcv.signal_range_max_ns);
    TEST_ASSERT_EQUAL_UINT(1u, mock_rmt.rcv.flags.en_partial_rx);
    /* No smaller than the hardware's block: a ping-pong half always fits. */
    TEST_ASSERT_EQUAL_UINT(SOC_RMT_MEM_WORDS_PER_CHANNEL * sizeof(rmt_symbol_word_t),
                           mock_rmt.buffer_size);
}

/* A DHT22-style reply: the level before the start level is skipped, every
 * level after it is an interval, and the quiet line ends the capture. */
static void test_capture_from_start_level(void)
{
    static const rmt_symbol_word_t f[] = {
        SYM(1, 30, 0, 80), SYM(1, 80, 0, 50), SYM(1, 26, 0, 50), SYM(1, 70, 1, 0),
    };

    mock_rmt_chunk(f, 4u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, iv, 31u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(6u, n);
    TEST_ASSERT_EQUAL_UINT16(80u, iv[0]);
    TEST_ASSERT_EQUAL_UINT16(80u, iv[1]);
    TEST_ASSERT_EQUAL_UINT16(50u, iv[2]);
    TEST_ASSERT_EQUAL_UINT16(26u, iv[3]);
    TEST_ASSERT_EQUAL_UINT16(50u, iv[4]);
    TEST_ASSERT_EQUAL_UINT16(70u, iv[5]);
    TEST_ASSERT_EQUAL_UINT16(0xA5A5u, iv[6]);
}

/* A frame that arrives in pieces (partial receive) and runs past max_edges:
 * the capture stops at max_edges, mid-symbol, and later pieces are not read. */
static void test_capture_stops_at_max_edges(void)
{
    static const rmt_symbol_word_t a[] = { SYM(0, 900, 1, 1000), SYM(0, 1100, 1, 1200) };
    static const rmt_symbol_word_t b[] = { SYM(0, 1300, 1, 1400), SYM(0, 1500, 1, 0) };

    mock_rmt_chunk(a, 2u, 0u);
    mock_rmt_chunk(b, 2u, 0u);
    mock_rmt_chunk(a, 2u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 3000u, &n));
    TEST_ASSERT_EQUAL_UINT8(4u, n);
    TEST_ASSERT_EQUAL_UINT16(1000u, iv[0]);
    TEST_ASSERT_EQUAL_UINT16(1100u, iv[1]);
    TEST_ASSERT_EQUAL_UINT16(1200u, iv[2]);
    TEST_ASSERT_EQUAL_UINT16(1300u, iv[3]);
    TEST_ASSERT_EQUAL_UINT16(0xA5A5u, iv[4]);
    TEST_ASSERT_EQUAL_UINT(2u, mock_rmt.delivered);
}

/* A pin that never moves: TIMEOUT, nothing written, and the wait is the
 * AVR's bound -- the first edge, max_edges intervals and the quiet line. */
static void test_quiet_pin_times_out(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, iv, 3u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_UINT16(0xA5A5u, iv[0]);
    TEST_ASSERT_TRUE(mock_rmt.now >= 5000);
    TEST_ASSERT_TRUE(mock_rmt.now <= 5000 + 2 * mock_rmt.step);
}

/* A frame that never ends: what arrived by the deadline is the capture. */
static void test_deadline_keeps_what_arrived(void)
{
    static const rmt_symbol_word_t a[] = { SYM(1, 400, 0, 600) };

    mock_rmt_chunk(a, 1u, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 8u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(2u, n);
    TEST_ASSERT_EQUAL_UINT16(400u, iv[0]);
    TEST_ASSERT_EQUAL_UINT16(600u, iv[1]);
}

/* Only the level before the quiet line: no edge-to-edge interval. */
static void test_held_level_is_no_interval(void)
{
    static const rmt_symbol_word_t f[] = { SYM(0, 200, 1, 0) };

    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, iv, 8u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
}

/* Above a 32767 us timeout a tick is 2 us; the idle threshold stays in 15 bits. */
static void test_long_timeout_ticks_2us(void)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 20000, 0, 32767), SYM(1, 1, 0, 0) };

    mock_rmt_chunk(f, 2u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 8u, 50000u, &n));
    TEST_ASSERT_EQUAL_UINT32(500000u, mock_rmt.cfg.resolution_hz);
    TEST_ASSERT_EQUAL_UINT32(50000000u, mock_rmt.rcv.signal_range_max_ns);
    TEST_ASSERT_EQUAL_UINT8(3u, n);
    TEST_ASSERT_EQUAL_UINT16(40000u, iv[0]);
    TEST_ASSERT_EQUAL_UINT16(65534u, iv[1]);
    TEST_ASSERT_EQUAL_UINT16(2u, iv[2]);
}

static void test_tick_boundary(void)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 5, 0, 0) };

    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 1u, 32767u, &n));
    TEST_ASSERT_EQUAL_UINT32(1000000u, mock_rmt.cfg.resolution_hz);
    TEST_ASSERT_EQUAL_UINT32(32767000u, mock_rmt.rcv.signal_range_max_ns);
    TEST_ASSERT_EQUAL_UINT16(5u, iv[0]);

    setUp();
    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 1u, 32768u, &n));
    TEST_ASSERT_EQUAL_UINT32(500000u, mock_rmt.cfg.resolution_hz);
    TEST_ASSERT_EQUAL_UINT16(10u, iv[0]);
}

/* A timeout past what 15 bits of 2 us hold is clamped, never refused. */
static void test_idle_clamped(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, iv, 1u, 100000u, &n));
    TEST_ASSERT_EQUAL_UINT32(65534000u, mock_rmt.rcv.signal_range_max_ns);
}

static void test_no_edges_asked(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, iv, 0u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_UINT(0u, mock_rmt.created);
}

/* The driver's failures, each with the channel given back. */
static void test_driver_errors(void)
{
    mock_rmt.new_err = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);

    setUp();
    mock_rmt.new_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));

    setUp();
    mock_rmt.cb_err = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT(1u, mock_rmt.deleted);

    setUp();
    mock_rmt.enable_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT(1u, mock_rmt.deleted);

    /* The classic esp32: no RX ping-pong, so no partial receive. */
    setUp();
    mock_rmt.receive_err = ESP_ERR_NOT_SUPPORTED;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_pulse_capture(&pin, 1u, iv, 4u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_rmt.enabled);
    TEST_ASSERT_EQUAL_UINT(1u, mock_rmt.deleted);
}

/* Back to back: the channel one call claimed is free for the next. */
static void test_channel_reused(void)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 10, 0, 0) };

    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, iv, 1u, 1000u, &n));
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, iv, 1u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT(2u, mock_rmt.created);
}

/* PULSE_IN: the first whole pulse at the level, the one before it skipped. */
static void test_pulse_in(void)
{
    static const rmt_symbol_word_t f[] = { SYM(1, 300, 0, 1500), SYM(1, 700, 0, 0) };
    uint32_t us = 0xDEADBEEFu;

    mock_rmt_chunk(f, 2u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 0u, 2000u, &us));
    TEST_ASSERT_EQUAL_UINT32(1500u, us);

    setUp();
    mock_rmt_chunk(f, 2u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 2000u, &us));
    TEST_ASSERT_EQUAL_UINT32(300u, us);
}

/* A pulse the line is still in when it goes quiet is no pulse; out untouched. */
static void test_pulse_in_unfinished(void)
{
    static const rmt_symbol_word_t f[] = { SYM(0, 300, 1, 0) };
    uint32_t us = 0xDEADBEEFu;

    mock_rmt_chunk(f, 1u, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 2000u, &us));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, us);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_config);
    RUN_TEST(test_capture_from_start_level);
    RUN_TEST(test_capture_stops_at_max_edges);
    RUN_TEST(test_quiet_pin_times_out);
    RUN_TEST(test_deadline_keeps_what_arrived);
    RUN_TEST(test_held_level_is_no_interval);
    RUN_TEST(test_long_timeout_ticks_2us);
    RUN_TEST(test_tick_boundary);
    RUN_TEST(test_idle_clamped);
    RUN_TEST(test_no_edges_asked);
    RUN_TEST(test_driver_errors);
    RUN_TEST(test_channel_reused);
    RUN_TEST(test_pulse_in);
    RUN_TEST(test_pulse_in_unfinished);
    return UNITY_END();
}
