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
#include "tfw/link.h"
#include "tfw/protocol.h"
#include "hal_eth_wire.h"
#include "link_eth.h"

/* --- fake UART ----------------------------------------------------------- */
static uint8_t  uart_in[64];
static unsigned uart_in_len, uart_in_pos;
static uint8_t  uart_out[64];
static unsigned uart_out_len;

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
    uart_out[uart_out_len++] = byte;
    return 1u;
}

/* --- fake listener ------------------------------------------------------- */
static uint8_t  sent[2048];
static unsigned sent_len;
static uint16_t send_room;              /* what the fake TCP's buffer has left */
static unsigned send_calls;
static unsigned recved_total;

void tfw_h5_wire_listen(void) { }

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
    uint8_t b;

    uart_in_len = uart_in_pos = uart_out_len = 0u;
    sent_len = send_calls = recved_total = finish_calls = 0u;
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tcp_bytes_are_read_and_the_reply_goes_back_on_tcp);
    RUN_TEST(test_events_stay_on_the_uart);
    RUN_TEST(test_hold_keeps_the_frame_on_its_wire);
    RUN_TEST(test_the_window_reopens_only_as_bytes_are_read);
    RUN_TEST(test_the_ring_takes_no_more_than_it_has_room_for);
    RUN_TEST(test_a_full_batch_flushes_and_a_full_tcp_buffer_waits);
    RUN_TEST(test_a_partly_sent_reply_finishes_on_the_next_poll);
    RUN_TEST(test_a_reply_owed_to_a_dropped_peer_is_discarded);
    RUN_TEST(test_a_new_connection_starts_clean);
    RUN_TEST(test_tcp_admits_the_ethernet_allow_list_only);
    RUN_TEST(test_a_half_closed_peer_is_answered_then_closed);
    return UNITY_END();
}
