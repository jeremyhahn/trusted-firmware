/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 tfw-wire TCP link (#582, hal_eth_wire.c) over a fake listener
 * seam and a fake UART: which wire a byte is read from, where its reply goes,
 * the receive window, the reply batching and a peer that goes away.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/link.h"
#include "tfw/protocol.h"
#include "hal_eth_wire.h"
#include "link_eth.h"

/* --- fake UART ----------------------------------------------------------- */
static uint8_t  uart_in[64];
static unsigned uart_in_len, uart_in_pos;
static uint8_t  uart_out[256];
static unsigned uart_out_len;
static unsigned uart_room = ~0u;        /* bytes the fake UART ring takes in all */

uint8_t tfw_hal_uart_rx(uint8_t *out)
{
    if (uart_in_pos == uart_in_len) {
        return 0u;
    }
    *out = uart_in[uart_in_pos++];
    return 1u;
}

uint8_t tfw_hal_uart_tx_try(uint8_t byte)
{
    if (uart_out_len >= uart_room || uart_out_len >= sizeof uart_out) {
        return 0u;                      /* the UART ring is full */
    }
    uart_out[uart_out_len++] = byte;
    return 1u;
}

/* --- fake listener ------------------------------------------------------- */
static uint8_t  sent[2048];
static unsigned sent_len;
static uint16_t send_room;              /* what the fake TCP's buffer has left */
static unsigned send_calls;
static unsigned recved_total;

void tfw_h5_wire_dial_poll(void) { }

uint16_t tfw_h5_wire_send(const uint8_t *buf, uint16_t len)
{
    uint16_t n = (len < send_room) ? len : send_room;

    send_calls++;
    memcpy(sent + sent_len, buf, n);
    sent_len += n;
    send_room = (uint16_t)(send_room - n);
    return n;
}

void tfw_h5_wire_recved(uint16_t n) { recved_total += n; }

static unsigned finish_calls;
static unsigned abort_calls;
static tfw_ms_t fake_ms;

tfw_ms_t tfw_hal_millis(void) { return fake_ms; }

/* As the listener does: reset the connection and forget the peer at once. */
void tfw_h5_wire_abort(void)
{
    abort_calls++;
    tfw_h5_wire_drop();
}

/* As the listener does once its send queue is empty: forget the peer. */
void tfw_h5_wire_finish(void)
{
    finish_calls++;
    tfw_h5_wire_drop();
}

static void uart_feed(const char *s)
{
    uart_in_len = (unsigned)strlen(s);
    uart_in_pos = 0u;
    memcpy(uart_in, s, uart_in_len);
}

static uint16_t tcp_feed(const char *s)
{
    return tfw_h5_wire_push((const uint8_t *)s, (uint16_t)strlen(s));
}

void setUp(void)
{
    uart_room = ~0u;
    uint8_t b;

    uart_in_len = uart_in_pos = uart_out_len = 0u;
    sent_len = send_calls = recved_total = finish_calls = abort_calls = 0u;
    fake_ms = 1000u;
    send_room = 0xFFFFu;
    tfw_h5_wire_drop();
    /* Leave the link on the UART, as a fresh boot is. */
    uart_feed("x");
    (void)tfw_link_rx(&b, 0u);
    uart_out_len = 0u;
}

void tearDown(void) { }

static void test_tcp_bytes_are_read_and_the_reply_goes_back_on_tcp(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(2, tcp_feed("ab"));
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_CHAR('a', b);
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 1u));
    TEST_ASSERT_EQUAL_CHAR('b', b);
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_rx(&b, 1u));

    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try('R', 0u));
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try('S', 0u));
    TEST_ASSERT_EQUAL_UINT(0, sent_len);            /* batched */
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(2, sent_len);
    TEST_ASSERT_EQUAL_MEMORY("RS", sent, 2);
    TEST_ASSERT_EQUAL_UINT(0, uart_out_len);
}

static void test_events_stay_on_the_uart(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("a");
    (void)tfw_link_rx(&b, 0u);
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try('E', 1u));
    tfw_link_tx_done(1u);
    TEST_ASSERT_EQUAL_UINT(1, uart_out_len);
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
}

static void test_hold_keeps_the_frame_on_its_wire(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("t");
    uart_feed("u");
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_CHAR('t', b);
    /* Mid-frame on TCP: the UART byte waits. */
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_rx(&b, 1u));
    /* Frame done: the UART is read and its reply goes there. */
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_CHAR('u', b);
    (void)tfw_link_tx_try('r', 0u);
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(1, uart_out_len);
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
}

static void test_the_window_reopens_only_as_bytes_are_read(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("abc");
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0, recved_total);
    (void)tfw_link_rx(&b, 0u);
    (void)tfw_link_rx(&b, 1u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(2, recved_total);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(2, recved_total);
}

static void test_the_ring_takes_no_more_than_it_has_room_for(void)
{
    static uint8_t big[TFW_H5_WIRE_RX_RING + 8u];

    tfw_h5_wire_open();
    memset(big, 'z', sizeof big);
    TEST_ASSERT_EQUAL_UINT16(TFW_H5_WIRE_RX_RING - 1u,
                             tfw_h5_wire_push(big, (uint16_t)sizeof big));
    TEST_ASSERT_EQUAL_UINT16(0, tfw_h5_wire_push(big, 1u));
}

static void test_bytes_read_from_a_full_ring_free_exactly_that_much_room(void)
{
    static uint8_t big[TFW_H5_WIRE_RX_RING + 8u];
    uint8_t        b = 0u;

    memset(big, 'z', sizeof big);
    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(TFW_H5_WIRE_RX_RING - 1u,
                             tfw_h5_wire_push(big, (uint16_t)sizeof big));
    TEST_ASSERT_EQUAL(1u, tfw_link_rx(&b, 0u));
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL(1u, tfw_link_rx(&b, 1u));
    }
    TEST_ASSERT_EQUAL_UINT16(5, tfw_h5_wire_push(big, 8u));
    TEST_ASSERT_EQUAL_UINT16(0, tfw_h5_wire_push(big, 1u));
}

static void test_bytes_pushed_across_the_ring_end_are_read_in_order(void)
{
    static uint8_t fill[2000u];
    uint8_t        seq[60];
    uint8_t        b = 0u;

    memset(fill, 'z', sizeof fill);
    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(2000, tfw_h5_wire_push(fill, (uint16_t)sizeof fill));
    TEST_ASSERT_EQUAL(1u, tfw_link_rx(&b, 0u));
    for (int i = 1; i < 2000; i++) {
        TEST_ASSERT_EQUAL(1u, tfw_link_rx(&b, 1u));
    }
    TEST_ASSERT_EQUAL(0u, tfw_link_rx(&b, 1u));

    for (int i = 0; i < 60; i++) {
        seq[i] = (uint8_t)(0x40 + i);
    }
    /* The head runs 2000..2047, then wraps to 0..11. */
    TEST_ASSERT_EQUAL_UINT16(60, tfw_h5_wire_push(seq, (uint16_t)sizeof seq));
    for (int i = 0; i < 60; i++) {
        TEST_ASSERT_EQUAL(1u, tfw_link_rx(&b, 1u));
        TEST_ASSERT_EQUAL_HEX8(seq[i], b);
    }
    TEST_ASSERT_EQUAL(0u, tfw_link_rx(&b, 1u));
}

static void test_a_uart_frame_holds_the_uart_while_a_tcp_byte_waits(void)
{
    uint8_t b = 0u;

    tfw_h5_wire_open();
    uart_feed("uv");
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_CHAR('u', b);
    TEST_ASSERT_EQUAL_UINT16(1, tcp_feed("t"));
    /* Mid-frame on the UART: the frame's next byte comes from the UART. */
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 1u));
    TEST_ASSERT_EQUAL_CHAR('v', b);
    /* The UART is empty and the frame still held: the TCP byte waits. */
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_rx(&b, 1u));
    /* Frame done: TCP is read and its reply goes back on TCP. */
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_CHAR('t', b);
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try('r', 0u));
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(1, sent_len);
    TEST_ASSERT_EQUAL_UINT(0, uart_out_len);
}

static void test_a_stall_across_the_millisecond_wrap_is_bounded_the_same(void)
{
    uint8_t b = 0u;

    fake_ms = (tfw_ms_t)0u - (TFW_H5_WIRE_TX_STALL_MS / 2u);
    tfw_h5_wire_open();
    (void)tcp_feed("a");
    (void)tfw_link_rx(&b, 0u);
    send_room = 0u;                                 /* the peer reads nothing */
    for (unsigned i = 0u; i < TFW_H5_WIRE_TX_BUF; i++) {
        TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try((uint8_t)i, 0u));
    }
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_tx_try(0xAA, 0u));   /* the stall starts */
    fake_ms += 1u;
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0, abort_calls);
    /* Past the counter's wrap, 1 ms short of the bound. */
    fake_ms += TFW_H5_WIRE_TX_STALL_MS - 2u;
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0, abort_calls);
    fake_ms += 1u;                                  /* the bound is reached */
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try(0xAA, 0u));
    TEST_ASSERT_EQUAL_UINT(1, abort_calls);
}

static void test_a_full_batch_flushes_and_a_full_tcp_buffer_waits(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("a");
    (void)tfw_link_rx(&b, 0u);
    for (unsigned i = 0u; i < TFW_H5_WIRE_TX_BUF; i++) {
        TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try((uint8_t)i, 0u));
    }
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
    send_room = 0u;                                 /* TCP's buffer is full */
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_tx_try(0xAA, 0u));
    send_room = 1000u;                              /* it drains */
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try(0xAA, 0u));
    TEST_ASSERT_EQUAL_UINT(TFW_H5_WIRE_TX_BUF, sent_len);
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(TFW_H5_WIRE_TX_BUF + 1u, sent_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, sent[TFW_H5_WIRE_TX_BUF]);
}

/* A TCP peer that stops reading: its reply waits TFW_H5_WIRE_TX_STALL_MS at
 * most, then the connection is reset, the reply dropped, and a command on the
 * UART is read and answered as before (#584). */
static void test_a_peer_that_stops_reading_is_reset_and_serial_is_answered(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("a");
    (void)tfw_link_rx(&b, 0u);
    send_room = 0u;                                 /* the peer reads nothing */
    for (unsigned i = 0u; i < TFW_H5_WIRE_TX_BUF; i++) {
        TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try((uint8_t)i, 0u));
    }
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_tx_try(0xAA, 0u));
    fake_ms += TFW_H5_WIRE_TX_STALL_MS - 1u;
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_tx_try(0xAA, 0u));
    TEST_ASSERT_EQUAL_UINT(0, abort_calls);
    fake_ms += 1u;                                  /* the bound is reached */
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try(0xAA, 0u));
    TEST_ASSERT_EQUAL_UINT(1, abort_calls);
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_take_reset());
    for (unsigned i = 0u; i < 16u; i++) {           /* the rest is dropped */
        TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try(0x55, 0u));
    }
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(0, sent_len);

    uart_feed("s");
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_HEX8('s', b);
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try(0x7E, 0u));
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(1, uart_out_len);
    TEST_ASSERT_EQUAL_HEX8(0x7E, uart_out[0]);
}

/* Progress, however slow, restarts the bound: a peer that reads a byte now
 * and then is not reset. */
static void test_a_slow_reader_is_not_reset(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("a");
    (void)tfw_link_rx(&b, 0u);
    send_room = 0u;
    for (unsigned i = 0u; i < TFW_H5_WIRE_TX_BUF; i++) {
        (void)tfw_link_tx_try((uint8_t)i, 0u);
    }
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_tx_try(0xAA, 0u));   /* the stall starts */
    for (unsigned i = 0u; i < 4u; i++) {
        fake_ms += TFW_H5_WIRE_TX_STALL_MS - 1u;
        send_room = 1u;                             /* one byte taken */
        tfw_link_poll();
    }
    TEST_ASSERT_EQUAL_UINT(0, abort_calls);
    TEST_ASSERT_EQUAL_UINT(4, sent_len);
}

static void test_a_partly_sent_reply_finishes_on_the_next_poll(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("a");
    (void)tfw_link_rx(&b, 0u);
    (void)tfw_link_tx_try('1', 0u);
    (void)tfw_link_tx_try('2', 0u);
    (void)tfw_link_tx_try('3', 0u);
    send_room = 2u;
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(2, sent_len);
    send_room = 8u;
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(3, sent_len);
    TEST_ASSERT_EQUAL_MEMORY("123", sent, 3);
}

static void test_a_reply_owed_to_a_dropped_peer_is_discarded(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("ab");
    (void)tfw_link_rx(&b, 0u);
    tfw_h5_wire_drop();
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_rx(&b, 1u));   /* its bytes are gone */
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try('r', 0u));
    tfw_link_tx_done(0u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
    TEST_ASSERT_EQUAL_UINT(0, uart_out_len);
    TEST_ASSERT_EQUAL_UINT16(0, tcp_feed("c"));         /* nothing to take */
    TEST_ASSERT_EQUAL_UINT(0, recved_total);
}

static void test_a_new_connection_starts_clean(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("old");
    (void)tfw_link_rx(&b, 0u);
    (void)tfw_link_tx_try('x', 0u);
    tfw_h5_wire_drop();                 /* the listener replaces it */
    tfw_h5_wire_open();
    (void)tcp_feed("N");
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_CHAR('N', b);
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1, recved_total);
}

static void test_tcp_admits_the_ethernet_allow_list_only(void)
{
    uint8_t b;

    /* The UART carries every command. */
    for (unsigned cmd = 0u; cmd < 256u; cmd++) {
        TEST_ASSERT_EQUAL_UINT8(1, tfw_link_admits((uint8_t)cmd));
    }
    /* A TCP peer only what link_eth.h allows: control stays on serial. */
    tfw_h5_wire_open();
    (void)tcp_feed("a");
    (void)tfw_link_rx(&b, 0u);
    for (unsigned cmd = 0u; cmd < 256u; cmd++) {
        TEST_ASSERT_EQUAL_UINT8(tfw_link_eth_allows((uint8_t)cmd),
                                tfw_link_admits((uint8_t)cmd));
    }
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_admits(TFW_CMD_CAPS));
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_admits(TFW_CMD_OTA_DATA));
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_admits(TFW_CMD_RESET));
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_admits(TFW_CMD_PING));
}

static void test_a_half_closed_peer_is_answered_then_closed(void)
{
    uint8_t b;

    tfw_h5_wire_open();
    (void)tcp_feed("ab");
    tfw_h5_wire_eof();                  /* FIN behind the request */
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0, finish_calls);        /* unread: kept */
    (void)tfw_link_rx(&b, 0u);
    (void)tfw_link_rx(&b, 1u);
    TEST_ASSERT_EQUAL_CHAR('b', b);
    send_room = 0u;                     /* the reply cannot go yet */
    (void)tfw_link_tx_try('R', 0u);
    tfw_link_tx_done(0u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0, finish_calls);        /* still owed */
    send_room = 8u;
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1, sent_len);
    TEST_ASSERT_EQUAL_UINT(1, finish_calls);
    TEST_ASSERT_EQUAL_UINT(2, recved_total);        /* window reopened first */
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1, finish_calls);        /* the wire has dropped */
}

/* The DHCP-bound line arrives while a long UART response is half in a full
 * ring: it waits, and goes out after the frame's last byte, not inside it. */
static void test_a_diagnostic_waits_for_the_frame_in_flight(void)
{
    static const char line[] = "eth: dhcp bound\r\n";
    uint8_t  frame[100];
    unsigned i;

    for (i = 0u; i < sizeof frame; i++) {
        frame[i] = (uint8_t)(0x80u + (i & 0x3fu));
    }
    uart_room = 40u;
    for (i = 0u; i < sizeof frame && tfw_link_tx_try(frame[i], 1u); i++) {
    }
    TEST_ASSERT_EQUAL_UINT(40, i);
    tfw_h5_wire_say(line);
    tfw_h5_wire_say_poll();
    TEST_ASSERT_EQUAL_UINT(40, uart_out_len);
    uart_room = ~0u;
    tfw_h5_wire_say_poll();
    TEST_ASSERT_EQUAL_UINT(40, uart_out_len);
    for (; i < sizeof frame; i++) {
        TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try(frame[i], 1u));
    }
    tfw_link_tx_done(1u);
    TEST_ASSERT_EQUAL_UINT(sizeof frame + sizeof line - 1u, uart_out_len);
    TEST_ASSERT_EQUAL_MEMORY(frame, uart_out, sizeof frame);
    TEST_ASSERT_EQUAL_MEMORY(line, uart_out + sizeof frame, sizeof line - 1u);
}

/* A line queued while the ring is full goes out whole before the next frame's
 * first byte; a line with no room left is dropped whole. */
static void test_a_frame_waits_for_a_queued_diagnostic(void)
{
    char big[TFW_H5_WIRE_SAY_BUF + 2u];

    uart_room = 3u;
    tfw_h5_wire_say("eth: phy none\r\n");
    TEST_ASSERT_EQUAL_UINT(3, uart_out_len);
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_tx_try('F', 1u));
    uart_room = ~0u;
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_tx_try('F', 1u));
    tfw_link_tx_done(1u);
    TEST_ASSERT_EQUAL_MEMORY("eth: phy none\r\nF", uart_out, 16);
    TEST_ASSERT_EQUAL_UINT(16, uart_out_len);

    memset(big, 'x', sizeof big - 1u);
    big[sizeof big - 1u] = '\0';
    tfw_h5_wire_say(big);
    TEST_ASSERT_EQUAL_UINT(16, uart_out_len);
}

/*
 * hal_eth_wire.c tfw_h5_wire_say keeps a line only when all of it fits in
 * the room left behind the lines still queued: a line that would overflow
 * say_buf is dropped whole, and a later line that fits is still queued.
 */
static void test_a_line_with_no_room_behind_a_queued_one_is_dropped_whole(void)
{
    char first[16];
    char wide[61];
    char last[21];

    memset(first, 'a', sizeof first - 1u);
    first[sizeof first - 1u] = '\0';
    memset(wide, 'b', sizeof wide - 1u);
    wide[sizeof wide - 1u] = '\0';
    memset(last, 'c', sizeof last - 1u);
    last[sizeof last - 1u] = '\0';
    TEST_ASSERT_TRUE(15u + 60u > TFW_H5_WIRE_SAY_BUF);
    TEST_ASSERT_TRUE(15u + 20u <= TFW_H5_WIRE_SAY_BUF);

    uart_room = 0u;
    tfw_h5_wire_say(first);
    tfw_h5_wire_say(wide);
    tfw_h5_wire_say(last);
    TEST_ASSERT_EQUAL_UINT(0, uart_out_len);

    uart_room = ~0u;
    tfw_h5_wire_say_poll();
    TEST_ASSERT_EQUAL_UINT(35, uart_out_len);
    TEST_ASSERT_EQUAL_MEMORY(first, uart_out, 15);
    TEST_ASSERT_EQUAL_MEMORY(last, uart_out + 15, 20);
    for (uint32_t k = 0u; k < uart_out_len; k++) {
        TEST_ASSERT_NOT_EQUAL('b', uart_out[k]);
    }
}

/* A TCP connect or disconnect while the UART is the current wire leaves the
 * transport's UART frame alone: no reset is raised (#599). */
static void test_a_tcp_peer_change_while_the_uart_is_current_keeps_its_frame(void)
{
    uint8_t b;

    (void)tfw_link_take_reset();        /* setUp's drop may have raised it */
    uart_feed("u");
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_CHAR('u', b);
    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_take_reset());
    tfw_h5_wire_drop();
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_take_reset());
}

/* A TCP peer dropped while TCP is the current wire raises the reset, and the
 * transport reads it once. */
static void test_a_tcp_peer_change_while_tcp_is_current_is_reported_once(void)
{
    uint8_t b;

    (void)tfw_link_take_reset();
    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(1, tcp_feed("t"));
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_CHAR('t', b);
    tfw_h5_wire_drop();
    TEST_ASSERT_EQUAL_UINT8(1, tfw_link_take_reset());
    TEST_ASSERT_EQUAL_UINT8(0, tfw_link_take_reset());
}

static void test_a_line_that_fills_the_buffer_exactly_goes_out_whole(void)
{
    char full[TFW_H5_WIRE_SAY_BUF + 1u];

    memset(full, 'f', sizeof full - 1u);
    full[sizeof full - 1u] = '\0';

    tfw_h5_wire_say(full);
    tfw_h5_wire_say_poll();
    TEST_ASSERT_EQUAL_UINT(TFW_H5_WIRE_SAY_BUF, uart_out_len);
    TEST_ASSERT_EQUAL_MEMORY(full, uart_out, TFW_H5_WIRE_SAY_BUF);
}

static void test_a_line_that_fills_the_room_exactly_is_kept_and_one_more_is_dropped(void)
{
    char first[16];
    char fits[50];
    char over[51];

    memset(first, 'a', sizeof first - 1u);
    first[sizeof first - 1u] = '\0';
    memset(fits, 'b', sizeof fits - 1u);
    fits[sizeof fits - 1u] = '\0';
    memset(over, 'c', sizeof over - 1u);
    over[sizeof over - 1u] = '\0';
    TEST_ASSERT_TRUE(15u + 49u == TFW_H5_WIRE_SAY_BUF);

    uart_room = 0u;
    tfw_h5_wire_say(first);
    tfw_h5_wire_say(fits);
    TEST_ASSERT_EQUAL_UINT(0, uart_out_len);
    uart_room = ~0u;
    tfw_h5_wire_say_poll();
    TEST_ASSERT_EQUAL_UINT(64, uart_out_len);
    TEST_ASSERT_EQUAL_MEMORY(first, uart_out, 15);
    TEST_ASSERT_EQUAL_MEMORY(fits, uart_out + 15, 49);

    uart_out_len = 0u;
    uart_room = 0u;
    tfw_h5_wire_say(first);
    tfw_h5_wire_say(over);
    TEST_ASSERT_EQUAL_UINT(0, uart_out_len);
    uart_room = ~0u;
    tfw_h5_wire_say_poll();
    TEST_ASSERT_EQUAL_UINT(15, uart_out_len);
    TEST_ASSERT_EQUAL_MEMORY(first, uart_out, 15);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tcp_bytes_are_read_and_the_reply_goes_back_on_tcp);
    RUN_TEST(test_events_stay_on_the_uart);
    RUN_TEST(test_hold_keeps_the_frame_on_its_wire);
    RUN_TEST(test_the_window_reopens_only_as_bytes_are_read);
    RUN_TEST(test_the_ring_takes_no_more_than_it_has_room_for);
    RUN_TEST(test_bytes_read_from_a_full_ring_free_exactly_that_much_room);
    RUN_TEST(test_bytes_pushed_across_the_ring_end_are_read_in_order);
    RUN_TEST(test_a_uart_frame_holds_the_uart_while_a_tcp_byte_waits);
    RUN_TEST(test_a_stall_across_the_millisecond_wrap_is_bounded_the_same);
    RUN_TEST(test_a_full_batch_flushes_and_a_full_tcp_buffer_waits);
    RUN_TEST(test_a_peer_that_stops_reading_is_reset_and_serial_is_answered);
    RUN_TEST(test_a_slow_reader_is_not_reset);
    RUN_TEST(test_a_partly_sent_reply_finishes_on_the_next_poll);
    RUN_TEST(test_a_reply_owed_to_a_dropped_peer_is_discarded);
    RUN_TEST(test_a_new_connection_starts_clean);
    RUN_TEST(test_tcp_admits_the_ethernet_allow_list_only);
    RUN_TEST(test_a_half_closed_peer_is_answered_then_closed);
    RUN_TEST(test_a_diagnostic_waits_for_the_frame_in_flight);
    RUN_TEST(test_a_frame_waits_for_a_queued_diagnostic);
    RUN_TEST(test_a_line_with_no_room_behind_a_queued_one_is_dropped_whole);
    RUN_TEST(test_a_tcp_peer_change_while_the_uart_is_current_keeps_its_frame);
    RUN_TEST(test_a_tcp_peer_change_while_tcp_is_current_is_reported_once);
    RUN_TEST(test_a_line_that_fills_the_buffer_exactly_goes_out_whole);
    RUN_TEST(test_a_line_that_fills_the_room_exactly_is_kept_and_one_more_is_dropped);
    return UNITY_END();
}
