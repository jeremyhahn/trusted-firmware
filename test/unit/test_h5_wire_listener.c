/*
 * The STM32H5 TCP listener (hal_eth_wire_tcp.c) with the link (hal_eth_wire.c)
 * and the real transport and framer above it, over a fake of lwIP's raw TCP
 * API (#584). lwIP delivers a segment that carries data and FIN as two receive
 * callbacks inside one tcp_input: the data, then p == NULL. The request must
 * still be answered, and the connection closed only once it is.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "transport.h"
#include "sys.h"
#include "hal_mock.h"
#include "tfw/link.h"
#include "tfw/protocol.h"
#include "hal_eth_wire.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"

/* --- fake lwIP raw TCP API ------------------------------------------------ */
const ip_addr_t ip_addr_any = IPADDR4_INIT(IPADDR_ANY);

static struct tcp_pcb  listen_pcb, conn;
static unsigned        new_calls;
static tcp_accept_fn   accept_cb;
static tcp_recv_fn     recv_cb;
static uint8_t         sent[TFW_MAX_FRAME * 4u];
static unsigned        sent_len;
static unsigned        close_calls, abort_calls;
static unsigned        sent_at_close;
static unsigned        recved_total;

struct tcp_pcb *tcp_new(void) { new_calls++; return &listen_pcb; }

err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port)
{
    (void)pcb; (void)ipaddr; (void)port;
    return ERR_OK;
}

struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *pcb, u8_t backlog)
{
    (void)backlog;
    return pcb;
}

void tcp_accept(struct tcp_pcb *pcb, tcp_accept_fn accept) { (void)pcb; accept_cb = accept; }
void tcp_arg(struct tcp_pcb *pcb, void *arg) { (void)pcb; (void)arg; }

void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn recv)
{
    if (pcb == &conn) {
        recv_cb = recv;
    }
}

void tcp_err(struct tcp_pcb *pcb, tcp_err_fn err) { (void)pcb; (void)err; }
void tcp_recved(struct tcp_pcb *pcb, u16_t len) { (void)pcb; recved_total += len; }
err_t tcp_output(struct tcp_pcb *pcb) { (void)pcb; return ERR_OK; }

err_t tcp_write(struct tcp_pcb *pcb, const void *dataptr, u16_t len, u8_t apiflags)
{
    (void)apiflags;
    memcpy(sent + sent_len, dataptr, len);
    sent_len += len;
    pcb->snd_queuelen++;                /* in flight until the test acks it */
    return ERR_OK;
}

err_t tcp_close(struct tcp_pcb *pcb)
{
    (void)pcb;
    close_calls++;
    sent_at_close = sent_len;
    return ERR_OK;
}

void tcp_abort(struct tcp_pcb *pcb) { (void)pcb; abort_calls++; }
u8_t pbuf_free(struct pbuf *p) { (void)p; return 1u; }

/* --- the suite ------------------------------------------------------------ */
static tfw_transport_t t;
static tfw_framer_t    rxf;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_transport_init(&t);
    tfw_framer_init(&rxf);
    sent_len = close_calls = abort_calls = sent_at_close = recved_total = 0u;
    memset(&conn, 0, sizeof conn);
    conn.snd_buf = 4096u;
    if (accept_cb == NULL) {
        tfw_h5_wire_listen();
    }
    TEST_ASSERT_NOT_NULL(accept_cb);
    TEST_ASSERT_EQUAL(ERR_OK, accept_cb(NULL, &conn, ERR_OK));
    TEST_ASSERT_NOT_NULL(recv_cb);
    abort_calls = 0u;                   /* a connection a test left open */
    (void)tfw_link_take_reset();
}

void tearDown(void) { }

/* 1 if what went back on TCP holds a reply under seq. */
static uint8_t answered(uint8_t seq)
{
    for (unsigned i = 0u; i < sent_len; i++) {
        if (tfw_framer_push(&rxf, sent[i]) == TFW_FRAME_READY &&
            tfw_frame_seq(&rxf) == seq) {
            return 1u;
        }
    }
    return 0u;
}

/* One tcp_input: the request's bytes, then (fin) the peer's FIN. */
static void tcp_input(const uint8_t *data, uint16_t len, uint8_t fin)
{
    struct pbuf p;

    memset(&p, 0, sizeof p);
    p.payload = (void *)(uintptr_t)data;
    p.len     = len;
    p.tot_len = len;
    if (len > 0u) {
        TEST_ASSERT_EQUAL(ERR_OK, recv_cb(NULL, &conn, &p, ERR_OK));
    }
    if (fin) {
        TEST_ASSERT_EQUAL(ERR_OK, recv_cb(NULL, &conn, NULL, ERR_OK));
    }
}

static void test_a_request_sent_with_fin_is_answered_before_the_close(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 7u, TFW_CMD_CAPS, 0, 0u);

    TEST_ASSERT_NOT_EQUAL(0u, len);
    tcp_input(wire, len, 1u);
    TEST_ASSERT_EQUAL_UINT(0, close_calls);         /* the request is kept */

    for (unsigned i = 0u; i < 4u; i++) { tfw_transport_poll(&t); }
    TEST_ASSERT_TRUE(answered(7u));
    TEST_ASSERT_EQUAL_UINT(len, recved_total);      /* window reopened */
    TEST_ASSERT_EQUAL_UINT(0, close_calls);         /* the reply is unacked */

    conn.snd_queuelen = 0u;                         /* the peer acks it */
    tfw_transport_poll(&t);
    TEST_ASSERT_EQUAL_UINT(1, close_calls);
    TEST_ASSERT_EQUAL_UINT(sent_len, sent_at_close);
    TEST_ASSERT_EQUAL_UINT(0, abort_calls);

    tfw_transport_poll(&t);
    TEST_ASSERT_EQUAL_UINT(1, close_calls);         /* closed once */
}

static void test_a_bare_fin_closes_on_the_next_poll(void)
{
    tcp_input(NULL, 0u, 1u);
    TEST_ASSERT_EQUAL_UINT(0, close_calls);
    tfw_transport_poll(&t);
    TEST_ASSERT_EQUAL_UINT(1, close_calls);
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
}

static void test_an_error_closes_at_once(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 3u, TFW_CMD_CAPS, 0, 0u);
    struct pbuf p;

    memset(&p, 0, sizeof p);
    p.payload = wire;
    p.len     = len;
    p.tot_len = len;
    TEST_ASSERT_EQUAL(ERR_OK, recv_cb(NULL, &conn, &p, ERR_ABRT));
    TEST_ASSERT_EQUAL_UINT(1, close_calls);
    for (unsigned i = 0u; i < 4u; i++) { tfw_transport_poll(&t); }
    TEST_ASSERT_FALSE(answered(3u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_request_sent_with_fin_is_answered_before_the_close);
    RUN_TEST(test_a_bare_fin_closes_on_the_next_poll);
    RUN_TEST(test_an_error_closes_at_once);
    return UNITY_END();
}
