/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutation tests for tfwsim's Ethernet stand-in (tools/tfwsim/eth_socket.c,
 * #55, #163): include/tfw/eth.h over a host TCP socket. The peer is a loopback
 * listener on a port the kernel picks, opened and closed by each test; nothing
 * waits on a clock, as loopback delivers a byte, a FIN or a RST before the
 * call that sent it returns. Each test names the faults it kills.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include "unity.h"
#include "tfw/eth.h"
#include "eth_socket.h"

static int          lsn  = -1;
static int          peer = -1;
static tfw_netcfg_t cfg;

/* A loopback socket bound to a port the kernel picks, written into cfg. */
static int bound_socket(void)
{
    struct sockaddr_in a;
    socklen_t          len = sizeof a;
    int                s   = socket(AF_INET, SOCK_STREAM, 0);

    TEST_ASSERT_TRUE(s >= 0);
    memset(&a, 0, sizeof a);
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TEST_ASSERT_EQUAL_INT(0, bind(s, (const struct sockaddr *)&a, sizeof a));
    TEST_ASSERT_EQUAL_INT(0, getsockname(s, (struct sockaddr *)&a, &len));
    memset(&cfg, 0, sizeof cfg);
    cfg.peer[0] = 127u; cfg.peer[3] = 1u;
    cfg.port    = ntohs(a.sin_port);
    return s;
}

/* Started against a listener, accepted, and up. */
static void connect_up(void)
{
    lsn = bound_socket();
    TEST_ASSERT_EQUAL_INT(0, listen(lsn, 2));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_eth_start(&cfg, 4000u));
    peer = accept(lsn, NULL, NULL);
    TEST_ASSERT_TRUE(peer >= 0);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
}

/* The peer goes away with a RST rather than a FIN. */
static void reset_peer(void)
{
    struct linger l = { 1, 0 };

    TEST_ASSERT_EQUAL_INT(0, setsockopt(peer, SOL_SOCKET, SO_LINGER, &l, sizeof l));
    (void)close(peer);
    peer = -1;
}

static void send_all(const uint8_t *b, size_t n)
{
    TEST_ASSERT_EQUAL_INT((int)n, (int)send(peer, b, n, 0));
}

void setUp(void) {}

void tearDown(void)
{
    tfw_eth_close();
    if (peer >= 0) { (void)close(peer); }
    if (lsn >= 0)  { (void)close(lsn); }
    peer = lsn = -1;
}

/* Mutant: the `!enabled` check dropped. Before tfwsim_eth_enable() the
 * controller is absent: nothing is opened and nothing is dialled. Runs first,
 * as enabling cannot be undone. */
static void test_before_enable_the_controller_is_absent(void)
{
    lsn = bound_socket();
    TEST_ASSERT_EQUAL_INT(0, listen(lsn, 1));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_eth_start(&cfg, 4000u));
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_eth_fd());
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
}

/* Mutants: BUSY -> UP skipped, or the idle peek's would_block() negated (an
 * idle connection closed). Up, and still up with nothing to read. */
static void test_a_listening_peer_comes_up_and_stays_up_idle(void)
{
    uint8_t b;

    connect_up();
    TEST_ASSERT_TRUE(tfwsim_eth_fd() >= 0);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&b));
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
}

/* Mutants: SO_ERROR's `err == 0` dropped, or a refused connect read as up.
 * A port bound and not listening refuses; the link reads down. */
static void test_a_refused_peer_reads_down(void)
{
    uint8_t b;

    lsn = bound_socket();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_eth_start(&cfg, 4000u));
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&b));
}

/* Mutants: the receive buffer's bound or position off by one, or a refill
 * that does not restart at 0. 600 bytes cross the 512-byte buffer once and
 * come out in order, then nothing. */
static void test_reads_cross_the_receive_buffer_in_order(void)
{
    uint8_t in[600], b;
    size_t  i;

    connect_up();
    for (i = 0u; i < sizeof in; i++) { in[i] = (uint8_t)(i * 7u + 1u); }
    send_all(in, sizeof in);
    for (i = 0u; i < sizeof in; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&b));
        TEST_ASSERT_EQUAL_HEX8(in[i], b);
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&b));
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
}

/* Mutants: rx's `n <= 0` -> `n < 0` (the FIN read as a byte), the peek's
 * `n == 0` dropped (a closed peer stays up), or the peek taken with bytes
 * still buffered. The peer's last bytes are read before its close is seen. */
static void test_a_closed_peer_is_seen_after_its_last_byte(void)
{
    static const uint8_t two[2] = { 0xA5, 0x5A };
    uint8_t b;

    connect_up();
    send_all(two, sizeof two);
    (void)close(peer);
    peer = -1;
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xA5, b);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x5A, b);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&b));
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_eth_fd());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&b));
}

/* Mutant: the peek's error that is not would_block() kept up. A RST is an
 * error, not an idle link. */
static void test_a_reset_peer_reads_down(void)
{
    connect_up();
    reset_peer();
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_eth_fd());
}

/* Mutants: the queue's `== TXQ_LEN` bound moved, the queue not emptied by a
 * send, or a send that writes from the wrong offset. 2048 bytes queue, the
 * 2049th does not, and the peer reads exactly the 2048 in order; the queue
 * then takes more. */
static void test_tx_queues_2048_bytes_and_sends_them_whole(void)
{
    static uint8_t got[4096];
    size_t  i, n = 0u;
    ssize_t r;

    connect_up();
    for (i = 0u; i < 2048u; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_tx((uint8_t)(i ^ (i >> 8))));
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_tx(0xEE));
    tfw_eth_send();
    while ((r = recv(peer, &got[n], sizeof got - n, MSG_DONTWAIT)) > 0) { n += (size_t)r; }
    TEST_ASSERT_EQUAL_size_t(2048u, n);
    for (i = 0u; i < 2048u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(i ^ (i >> 8)), got[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_tx(0x42));
    tfw_eth_send();
    TEST_ASSERT_EQUAL_INT(1, (int)recv(peer, got, sizeof got, MSG_DONTWAIT));
    TEST_ASSERT_EQUAL_HEX8(0x42, got[0]);
}

/* Mutant: a failed send that does not close (the loop then never ends, or
 * the link reads up). A send to a reset peer drops the link and the queue. */
static void test_a_send_to_a_reset_peer_closes_the_link(void)
{
    size_t i;

    connect_up();
    reset_peer();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_tx(0x01));
    tfw_eth_send();
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_eth_fd());
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
    for (i = 0u; i < 2048u; i++) { TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_tx(0x00)); }
}

/* Mutants: close leaving rx_len, rx_pos or tx_len as they were. Bytes read
 * into the buffer and bytes queued do not outlive the connection. */
static void test_close_drops_buffered_and_queued_bytes(void)
{
    static const uint8_t five[5] = { 1, 2, 3, 4, 5 };
    uint8_t b;
    size_t  i;

    connect_up();
    send_all(five, sizeof five);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_rx(&b));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_tx(0x77));
    tfw_eth_close();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_rx(&b));
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_eth_fd());
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
    for (i = 0u; i < 2048u; i++) { TEST_ASSERT_EQUAL_UINT8(1u, tfw_eth_tx(0x00)); }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_eth_tx(0x00));
}

/* Mutant: start without closing what was open. A restart hangs up on the
 * old peer (it reads a FIN) and comes up on the new one. */
static void test_a_restart_hangs_up_the_old_connection(void)
{
    uint8_t b;
    int     old;

    connect_up();
    old = peer;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_eth_start(&cfg, 4000u));
    peer = accept(lsn, NULL, NULL);
    TEST_ASSERT_TRUE(peer >= 0);
    TEST_ASSERT_EQUAL_INT(0, (int)recv(old, &b, 1u, MSG_DONTWAIT));
    (void)close(old);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, tfw_eth_state());
}

/* Mutant: a failed socket() answered TFW_OK (the link then waits on a
 * connection that was never dialled). With no descriptor left to take, the
 * controller answers absent and holds nothing. */
static void test_no_descriptor_left_reads_absent(void)
{
    struct rlimit old, lim;
    tfw_status_t  st;
    int           next;

    lsn  = bound_socket();
    next = dup(lsn);
    TEST_ASSERT_TRUE(next >= 0);
    (void)close(next);
    TEST_ASSERT_EQUAL_INT(0, getrlimit(RLIMIT_NOFILE, &old));
    lim          = old;
    lim.rlim_cur = (rlim_t)next;
    TEST_ASSERT_EQUAL_INT(0, setrlimit(RLIMIT_NOFILE, &lim));
    st = tfw_eth_start(&cfg, 4000u);
    TEST_ASSERT_EQUAL_INT(0, setrlimit(RLIMIT_NOFILE, &old));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, st);
    TEST_ASSERT_EQUAL_INT(-1, tfwsim_eth_fd());
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, tfw_eth_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_before_enable_the_controller_is_absent);
    tfwsim_eth_enable();
    RUN_TEST(test_a_listening_peer_comes_up_and_stays_up_idle);
    RUN_TEST(test_a_refused_peer_reads_down);
    RUN_TEST(test_reads_cross_the_receive_buffer_in_order);
    RUN_TEST(test_a_closed_peer_is_seen_after_its_last_byte);
    RUN_TEST(test_a_reset_peer_reads_down);
    RUN_TEST(test_tx_queues_2048_bytes_and_sends_them_whole);
    RUN_TEST(test_a_send_to_a_reset_peer_closes_the_link);
    RUN_TEST(test_close_drops_buffered_and_queued_bytes);
    RUN_TEST(test_a_restart_hangs_up_the_old_connection);
    RUN_TEST(test_no_descriptor_left_reads_absent);
    return UNITY_END();
}
