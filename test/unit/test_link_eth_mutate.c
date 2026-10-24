/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Ethernet image's link under a socket that answers anything.
 *
 * Linked as test_link_eth.c is -- the host library with link_eth.c where
 * link_serial.c would be, the TCP socket faked here at include/tfw/eth.h --
 * with the fake made hostile: tfw_eth_state() flips between every state and
 * values no driver returns, tfw_eth_start() fails at random, tfw_eth_tx()
 * refuses at random points of a reply, and tfw_eth_rx() hands up request
 * frames with bits flipped and tails cut, or noise, with gaps between bytes.
 *
 * A seeded xorshift32 drives it for a fixed count. Every call must return,
 * and what the link sends must fit the reply buffer of one request. Then the
 * socket is made honest, and CAPS over Ethernet answers exactly as
 * test_link_eth pins it: its seq, OK, one SEND.
 */

#include <string.h>
#include "unity.h"
#include "transport.h"
#include "sys.h"
#include "hal_mock.h"
#include "tfw/eth.h"
#include "tfw/link.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"

#define ITERATIONS 3000u
#define SEED       0x2545F491u

static uint32_t rng;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* --------------------------------------------------------------- the socket */

static struct {
    uint8_t      hostile;
    uint8_t      state;
    unsigned     starts, closes, sends;
    uint8_t      in[TFW_MAX_FRAME];
    uint16_t     in_len, in_pos;
    uint8_t      q[256];          /* tfw_eth_tx() so far, not yet sent */
    uint16_t     q_len, q_cap;
    uint8_t      out[512];        /* what tfw_eth_send() put on the wire */
    uint16_t     out_len;
    uint32_t     over;            /* sends that would not fit out[] */
} eth;

tfw_status_t tfw_eth_start(const tfw_netcfg_t *cfg, uint16_t local_port)
{
    (void)cfg;
    (void)local_port;
    eth.starts++;
    if (eth.hostile && (next() % 4u) == 0u) {
        return (next() & 1u) ? TFW_ERR_UNSUPPORTED : TFW_ERR_INTERNAL;
    }
    eth.state = TFW_ETH_BUSY;
    return TFW_OK;
}

uint8_t tfw_eth_state(void)
{
    static const uint8_t pick[] = { TFW_ETH_DOWN, TFW_ETH_BUSY, TFW_ETH_UP, TFW_ETH_UP };

    if (eth.hostile && (next() % 8u) == 0u) {
        eth.state = (next() % 5u) ? pick[next() % sizeof pick] : (uint8_t)next();
    }
    return eth.state;
}

uint8_t tfw_eth_rx(uint8_t *out)
{
    if (eth.in_pos == eth.in_len || (eth.hostile && (next() % 16u) == 0u)) {
        return 0u;
    }
    *out = eth.in[eth.in_pos++];
    return 1u;
}

uint8_t tfw_eth_tx(uint8_t byte)
{
    if (eth.q_len == eth.q_cap || (eth.hostile && (next() % 64u) == 0u)) {
        return 0u;
    }
    eth.q[eth.q_len++] = byte;
    return 1u;
}

void tfw_eth_send(void)
{
    if ((size_t)eth.out_len + eth.q_len > sizeof eth.out) {
        eth.over++;
        eth.out_len = 0u;
    }
    memcpy(&eth.out[eth.out_len], eth.q, eth.q_len);
    eth.out_len = (uint16_t)(eth.out_len + eth.q_len);
    eth.q_len = 0u;
    eth.sends++;
}

void tfw_eth_close(void)
{
    eth.closes++;
    eth.state = TFW_ETH_DOWN;
}

/* -------------------------------------------------------------- the fixture */

/* test_link_eth's literal block: MAC 02:00:00:00:00:01, device 192.0.2.10/24
 * via 192.0.2.1, endpoint 192.0.2.20:7020. */
static const uint8_t NETCFG[TFW_NETCFG_LEN] = {
    'T', 'F', 'N', 'C', 0x01, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
    192, 0, 2, 10,   255, 255, 255, 0,   192, 0, 2, 1,   192, 0, 2, 20,
    0x1B, 0x6C,
    0x79, 0xF8,
};

#define LONGEST_BACKOFF_MS 64000u

static tfw_transport_t t;
static tfw_framer_t    rxf;
static tfw_ms_t        clock_ms;

/* test_link_eth's setUp: the last connection dropped, the clock past the
 * longest backoff, so the link is DOWN and due. */
static void honest(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_transport_init(&t);
    tfw_framer_init(&rxf);

    clock_ms += 1000000u;
    tfw_mock_set_millis(clock_ms);
    memset(&eth, 0, sizeof eth);
    eth.q_cap = (uint16_t)sizeof eth.q;
    tfw_link_poll();
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    eth.starts = eth.closes = 0u;
}

void setUp(void)    { honest(); }
void tearDown(void) {}

static void connect_up(void)
{
    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], NETCFG, sizeof NETCFG);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
    eth.state = TFW_ETH_UP;
    tfw_link_poll();
}

static uint8_t decode(const uint8_t *b, size_t n)
{
    size_t i;

    tfw_framer_init(&rxf);
    for (i = 0; i < n; i++) {
        if (tfw_framer_push(&rxf, b[i]) == TFW_FRAME_READY) {
            return 1u;
        }
    }
    return 0u;
}

/* A request frame, a CAPS or a PING with a random payload, with a few bits
 * flipped and the tail cut; or noise. Lands in the socket's receive side. */
static void peer_frame(void)
{
    static const uint8_t cmd[] = { TFW_CMD_CAPS, TFW_CMD_PING };
    uint8_t  pay[16], i, flips;
    uint16_t n;

    eth.in_len = eth.in_pos = 0u;
    if (next() % 4u) {
        for (i = 0u; i < sizeof pay; i++) {
            pay[i] = (uint8_t)next();
        }
        n = tfw_frame_encode_request(eth.in, TFW_MAX_FRAME, (uint8_t)next(),
                                     cmd[next() % sizeof cmd], pay,
                                     (uint8_t)(next() % (sizeof pay + 1u)));
        TEST_ASSERT_NOT_EQUAL(0u, n);
        for (flips = (uint8_t)(next() % 3u); flips > 0u; flips--) {
            eth.in[next() % n] ^= (uint8_t)(1u << (next() % 8u));
        }
        if (next() & 1u) {
            n = (uint16_t)(next() % (n + 1u));
        }
    } else {
        n = (uint16_t)(next() % (sizeof eth.in + 1u));
        for (i = 0u; i < n; i++) {
            eth.in[i] = (uint8_t)next();
        }
    }
    eth.in_len = n;
}

/* ---------------------------------------------------------------- the run */

static void test_a_hostile_socket_never_wedges_the_link_and_caps_answers_after(void)
{
    uint32_t it, polls = 0u, rx = 0u;
    unsigned p;

    rng = SEED;
    connect_up();
    eth.hostile = 1u;
    for (it = 0u; it < ITERATIONS; it++) {
        peer_frame();
        eth.q_cap = (uint16_t)(next() % (sizeof eth.q + 1u));
        eth.out_len = 0u;
        for (p = (unsigned)(next() % 24u); p > 0u; p--) {
            tfw_transport_poll(&t);
            polls++;
            if ((next() % 4u) == 0u) {
                tfw_mock_advance((tfw_ms_t)(next() % 70000u));
            }
        }
        rx += eth.in_pos;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, eth.over, "more sent than one reply holds");
        TEST_ASSERT_TRUE(eth.in_pos <= eth.in_len);
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, polls);
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, rx);
    TEST_ASSERT_NOT_EQUAL(0u, eth.starts);
    TEST_ASSERT_NOT_EQUAL(0u, eth.sends);

    /* Honest again, with the transport and the link as the run left them --
     * the device does not restart. The peer reconnects, and CAPS over
     * Ethernet answers as test_link_eth pins it. Two drops leave the link
     * DOWN whichever phase it was in: the first may start it. */
    eth.hostile = 0u;
    eth.in_len = eth.in_pos = 0u;
    eth.q_cap = (uint16_t)sizeof eth.q;
    for (p = 0u; p < 8u; p++) {
        tfw_transport_poll(&t);
    }
    (void)tfw_mock_drain_tx(eth.out, sizeof eth.out);
    eth.state = TFW_ETH_DOWN;
    tfw_link_poll();
    eth.state = TFW_ETH_DOWN;
    tfw_link_poll();
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    eth.starts = eth.sends = 0u;
    eth.out_len = 0u;
    connect_up();
    eth.in_len = tfw_frame_encode_request(eth.in, TFW_MAX_FRAME, 0x31u, TFW_CMD_CAPS, 0, 0u);
    TEST_ASSERT_NOT_EQUAL(0u, eth.in_len);
    for (p = 0u; p < 8u; p++) {
        tfw_transport_poll(&t);
    }
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_tx_pending());
    TEST_ASSERT_TRUE(decode(eth.out, eth.out_len));
    TEST_ASSERT_EQUAL_HEX8(0x31u, tfw_frame_seq(&rxf));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT(1u, eth.sends);
}

/* The link with one byte taken off an UP connection, so the reply wire is
 * Ethernet; the connection is then dropped, or left opening, by the caller. */
static void reply_owed_over_eth(void)
{
    uint8_t b = 0u;

    connect_up();
    eth.in[0] = 0x5Au;
    eth.in_len = 1u;
    eth.in_pos = 0u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
    eth.in_len = eth.in_pos = 0u;
}

static void drop_then_reopen(void)
{
    eth.state = TFW_ETH_DOWN;
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.closes);
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(2u, eth.starts);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_BUSY, eth.state);
}

static void test_a_byte_waiting_on_a_connection_still_opening_is_not_read(void)
{
    uint8_t b = 0xA5u;

    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], NETCFG, sizeof NETCFG);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
    eth.in[0] = 0x5Au;
    eth.in_len = 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_link_rx(&b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, b);
    TEST_ASSERT_EQUAL_UINT16(0u, eth.in_pos);
}

static void test_a_connection_still_opening_is_neither_closed_nor_redialled(void)
{
    unsigned p;

    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], NETCFG, sizeof NETCFG);
    tfw_link_poll();
    for (p = 0u; p < 8u; p++) {
        tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
        tfw_link_poll();
    }
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
    TEST_ASSERT_EQUAL_UINT(0u, eth.closes);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_BUSY, eth.state);
}

static void test_a_held_ethernet_frame_does_not_read_serial_once_the_connection_drops(void)
{
    static const uint8_t s = 0x3Cu;
    uint8_t b = 0xA5u;

    reply_owed_over_eth();
    eth.state = TFW_ETH_DOWN;
    tfw_link_poll();
    tfw_mock_feed_rx(&s, 1u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_link_rx(&b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, b);
    /* The serial byte is still there for the next frame. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_rx(&b, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, b);
}

static void test_a_reply_byte_to_a_connection_still_opening_is_dropped_and_queues_nothing(void)
{
    reply_owed_over_eth();
    drop_then_reopen();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_tx_try(0xAAu, 0u));
    TEST_ASSERT_EQUAL_UINT16(0u, eth.q_len);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_tx_pending());
}

static void test_the_end_of_a_reply_to_a_connection_still_opening_sends_nothing(void)
{
    reply_owed_over_eth();
    drop_then_reopen();
    tfw_link_tx_done(0u);
    TEST_ASSERT_EQUAL_UINT(0u, eth.sends);
}

static void test_the_end_of_a_serial_reply_never_sends_on_ethernet(void)
{
    reply_owed_over_eth();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_tx_try(0xAAu, 1u));
    TEST_ASSERT_EQUAL_UINT16(0u, eth.q_len);
    tfw_link_tx_done(1u);
    TEST_ASSERT_EQUAL_UINT(0u, eth.sends);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_hostile_socket_never_wedges_the_link_and_caps_answers_after);
    RUN_TEST(test_a_byte_waiting_on_a_connection_still_opening_is_not_read);
    RUN_TEST(test_a_connection_still_opening_is_neither_closed_nor_redialled);
    RUN_TEST(test_a_held_ethernet_frame_does_not_read_serial_once_the_connection_drops);
    RUN_TEST(test_a_reply_byte_to_a_connection_still_opening_is_dropped_and_queues_nothing);
    RUN_TEST(test_the_end_of_a_reply_to_a_connection_still_opening_sends_nothing);
    RUN_TEST(test_the_end_of_a_serial_reply_never_sends_on_ethernet);
    return UNITY_END();
}
