/*
 * The STM32H5 wire's outbound TCP connection (hal_eth_wire_tcp.c) with the
 * link (hal_eth_wire.c) and the real transport and framer above it, over a
 * fake of lwIP's raw TCP API (#584, Codex round 4, ruling 989). The device
 * opens the one connection to the endpoint in its network block and listens
 * on nothing (docs/SPEC.md, ethernet-transport); a refused or dropped
 * connection is dialled again after a backoff, on the block read afresh.
 * lwIP delivers a segment that carries data and FIN as two receive callbacks
 * inside one tcp_input: the data, then p == NULL. The request must still be
 * answered, and the connection closed only once it is.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "transport.h"
#include "sys.h"
#include "crc16.h"
#include "hal_mock.h"
#include "tfw/eth.h"
#include "tfw/link.h"
#include "tfw/protocol.h"
#include "hal_eth_wire.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"

/* --- fake lwIP raw TCP API ------------------------------------------------ */
static struct tcp_pcb   conn;
static unsigned         new_calls, connect_calls, bind_calls, listen_calls;
static uint32_t         connect_ip;             /* network order, as lwIP */
static u16_t            connect_port;
static uint8_t          dialing;                /* a connect not yet answered */
static tcp_connected_fn connected_cb;
static tcp_recv_fn      recv_cb;
static tcp_err_fn       err_cb;
static void            *cb_arg;
static uint8_t          sent[TFW_MAX_FRAME * 4u];
static unsigned         sent_len;
static unsigned         close_calls, abort_calls;
static unsigned         sent_at_close;
static unsigned         recved_total;
static uint8_t          fail_new;               /* tcp_new: out of pcbs */
static err_t            fail_connect, fail_write, fail_close;

/* The netif (hal_eth_netif.c), faked: what the wire asks it before a dial. */
static unsigned reconcile_calls;
static unsigned reconcile_at_connect;   /* connect_calls when it was asked */
static uint16_t reconcile_port;
static int      reconcile_ready = 1;

int tfw_h5_eth_reconcile(const tfw_netcfg_t *cfg)
{
    reconcile_calls++;
    reconcile_at_connect = connect_calls;
    reconcile_port = cfg->port;
    return reconcile_ready;
}

struct tcp_pcb *tcp_new(void)
{
    new_calls++;
    if (fail_new) {
        return NULL;
    }
    memset(&conn, 0, sizeof conn);
    conn.snd_buf = 4096u;
    return &conn;
}

err_t tcp_connect(struct tcp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port,
                  tcp_connected_fn connected)
{
    (void)pcb;
    connect_calls++;
    connect_ip = ip4_addr_get_u32(ip_2_ip4(ipaddr));
    connect_port = port;
    connected_cb = connected;
    if (fail_connect != ERR_OK) {
        return fail_connect;
    }
    dialing = 1u;
    return ERR_OK;
}

/* A listener is not the contract: these only count, and must stay at 0. */
err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port)
{
    (void)pcb; (void)ipaddr; (void)port;
    bind_calls++;
    return ERR_OK;
}

struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *pcb, u8_t backlog)
{
    (void)backlog;
    listen_calls++;
    return pcb;
}

void tcp_accept(struct tcp_pcb *pcb, tcp_accept_fn accept) { (void)pcb; (void)accept; listen_calls++; }
void tcp_arg(struct tcp_pcb *pcb, void *arg) { (void)pcb; cb_arg = arg; }
void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn recv) { (void)pcb; recv_cb = recv; }
void tcp_err(struct tcp_pcb *pcb, tcp_err_fn err) { (void)pcb; err_cb = err; }
void tcp_recved(struct tcp_pcb *pcb, u16_t len) { (void)pcb; recved_total += len; }
err_t tcp_output(struct tcp_pcb *pcb) { (void)pcb; return ERR_OK; }

err_t tcp_write(struct tcp_pcb *pcb, const void *dataptr, u16_t len, u8_t apiflags)
{
    (void)apiflags;
    if (fail_write != ERR_OK) {
        return fail_write;
    }
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
    return fail_close;
}

void tcp_abort(struct tcp_pcb *pcb) { (void)pcb; abort_calls++; }
u8_t pbuf_free(struct pbuf *p) { (void)p; return 1u; }

/* --- the network block: RFC 5737 endpoint, a 02:00:00 test MAC ------------- */
static void put_block(uint16_t port)
{
    static const uint8_t head[30] = {
        'T', 'F', 'N', 'C', TFW_NETCFG_VERSION, 0u,
        0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u,
        192u, 0u, 2u, 20u, 255u, 255u, 255u, 0u, 192u, 0u, 2u, 1u,
        192u, 0u, 2u, 50u, 0u, 0u,
    };
    uint8_t *ee = tfw_mock_eeprom() + TFW_NETCFG_ADDR;
    uint16_t crc;

    memcpy(ee, head, sizeof head);
    ee[28] = (uint8_t)(port >> 8);
    ee[29] = (uint8_t)port;
    crc = tfw_crc16(ee, 30u);
    ee[30] = (uint8_t)crc;
    ee[31] = (uint8_t)(crc >> 8);
}

/* --- the suite ------------------------------------------------------------ */
static tfw_transport_t t;
static tfw_framer_t    rxf;

static void connect_answers(err_t err)
{
    TEST_ASSERT_TRUE(dialing);
    dialing = 0u;
    if (err == ERR_OK) {
        TEST_ASSERT_NOT_NULL(connected_cb);
        TEST_ASSERT_EQUAL(ERR_OK, connected_cb(cb_arg, &conn, ERR_OK));
    } else {
        TEST_ASSERT_NOT_NULL(err_cb);
        err_cb(cb_arg, err);            /* lwIP has freed the pcb */
    }
}

static tfw_ms_t epoch;

void setUp(void)
{
    fail_new = 0u;
    fail_connect = fail_write = fail_close = ERR_OK;
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_transport_init(&t);
    tfw_framer_init(&rxf);
    put_block(4000u);
    tfw_h5_wire_abort();                /* a connection a test left open */
    if (dialing) {
        connect_answers(ERR_ABRT);
    }
    /* tfw_mock_reset restarts the clock: each setUp moves it later than the
     * last one did, past any backoff the test before began. */
    epoch++;
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MAX_MS * 4u * epoch);
    new_calls = connect_calls = bind_calls = listen_calls = 0u;
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    connect_answers(ERR_OK);
    TEST_ASSERT_NOT_NULL(recv_cb);
    new_calls = connect_calls = 0u;     /* connect_ip and connect_port stay */
    sent_len = close_calls = abort_calls = sent_at_close = recved_total = 0u;
    (void)tfw_link_take_reset();
}

void tearDown(void) { }

static uint8_t answered(uint8_t seq)
{
    tfw_framer_init(&rxf);
    for (unsigned i = 0u; i < sent_len; i++) {
        if (tfw_framer_push(&rxf, sent[i]) == TFW_FRAME_READY &&
            tfw_frame_seq(&rxf) == seq) {
            return 1u;
        }
    }
    return 0u;
}

static void tcp_input(const uint8_t *data, uint16_t len, uint8_t fin)
{
    struct pbuf p;

    memset(&p, 0, sizeof p);
    p.payload = (void *)(uintptr_t)data;
    p.len     = len;
    p.tot_len = len;
    if (len > 0u) {
        TEST_ASSERT_EQUAL(ERR_OK, recv_cb(cb_arg, &conn, &p, ERR_OK));
    }
    if (fin) {
        TEST_ASSERT_EQUAL(ERR_OK, recv_cb(cb_arg, &conn, NULL, ERR_OK));
    }
}

static void request_is_answered(uint8_t seq)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, seq, TFW_CMD_CAPS, 0, 0u);

    TEST_ASSERT_NOT_EQUAL(0u, len);
    tcp_input(wire, len, 0u);
    for (unsigned i = 0u; i < 4u; i++) { tfw_transport_poll(&t); }
    TEST_ASSERT_TRUE(answered(seq));
}

static void test_the_device_dials_the_block_endpoint_and_binds_nothing(void)
{
    /* setUp's dial: the block's endpoint, 192.0.2.50 port 4000. */
    TEST_ASSERT_EQUAL_HEX32(PP_HTONL(LWIP_MAKEU32(192u, 0u, 2u, 50u)), connect_ip);
    TEST_ASSERT_EQUAL_UINT16(4000u, connect_port);
    TEST_ASSERT_EQUAL_UINT(0, bind_calls);
    TEST_ASSERT_EQUAL_UINT(0, listen_calls);

    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MAX_MS);
    tfw_h5_wire_dial_poll();            /* connected: nothing more is dialled */
    TEST_ASSERT_EQUAL_UINT(0, new_calls);
    TEST_ASSERT_EQUAL_UINT(0, connect_calls);

    request_is_answered(5u);            /* frames flow both ways */
}

static void test_a_refused_dial_is_retried_on_the_block_read_again(void)
{
    TEST_ASSERT_NOT_NULL(err_cb);
    err_cb(cb_arg, ERR_RST);            /* the endpoint drops the connection */
    put_block(4001u);

    tfw_h5_wire_dial_poll();            /* not at once: a backoff first */
    TEST_ASSERT_EQUAL_UINT(0, connect_calls);
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    TEST_ASSERT_EQUAL_UINT16(4001u, connect_port);      /* never cached */

    connect_answers(ERR_RST);           /* refused */
    put_block(4002u);
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();            /* the backoff has doubled */
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(2, connect_calls);
    TEST_ASSERT_EQUAL_UINT16(4002u, connect_port);

    connect_answers(ERR_OK);
    request_is_answered(9u);
    TEST_ASSERT_EQUAL_UINT(0, bind_calls);
    TEST_ASSERT_EQUAL_UINT(0, listen_calls);
}

static void test_a_missing_block_dials_nothing(void)
{
    tfw_h5_wire_abort();
    tfw_mock_eeprom()[TFW_NETCFG_ADDR + 31u] ^= 0xFFu;  /* bad CRC */
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MAX_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(0, connect_calls);
    TEST_ASSERT_EQUAL_UINT(0, new_calls);
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
    TEST_ASSERT_EQUAL(ERR_OK, recv_cb(cb_arg, &conn, &p, ERR_ABRT));
    TEST_ASSERT_EQUAL_UINT(1, close_calls);
    for (unsigned i = 0u; i < 4u; i++) { tfw_transport_poll(&t); }
    TEST_ASSERT_FALSE(answered(3u));
}

static void test_a_redial_reconciles_the_netif_with_the_block_it_dials(void)
{
    err_cb(cb_arg, ERR_RST);
    put_block(4003u);                   /* written over serial meanwhile */
    reconcile_calls = 0u;
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, reconcile_calls);         /* one block read */
    TEST_ASSERT_EQUAL_UINT(0, reconcile_at_connect);    /* before the connect */
    TEST_ASSERT_EQUAL_UINT16(4003u, reconcile_port);
    TEST_ASSERT_EQUAL_UINT16(4003u, connect_port);      /* the same block */
    connect_answers(ERR_OK);
}

static void test_no_dial_while_the_netif_is_not_ready_on_the_block(void)
{
    err_cb(cb_arg, ERR_RST);
    reconcile_ready = 0;                /* say a new MAC: the link comes up again */
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    reconcile_ready = 1;
    TEST_ASSERT_EQUAL_UINT(0, new_calls);
    TEST_ASSERT_EQUAL_UINT(0, connect_calls);
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    connect_answers(ERR_OK);
}

/* A peer that loses its TCP state without FIN or RST, with nothing queued to
 * it: the connection probes the peer with bounded keepalives, and when they
 * go unanswered lwIP's slow timer frees the pcb and reports ERR_ABRT, so the
 * device drops the stale connection and dials again (#584, Codex round 7). */
static void test_a_silent_peer_is_found_dead_by_keepalive_and_redialled(void)
{
#if LWIP_TCP_KEEPALIVE
    uint32_t dead = conn.keep_idle + conn.keep_intvl * conn.keep_cnt;

    TEST_ASSERT_TRUE(ip_get_option(&conn, SOF_KEEPALIVE) != 0);
    TEST_ASSERT_TRUE(conn.keep_idle > 0u);
    TEST_ASSERT_TRUE(conn.keep_intvl > 0u);
    TEST_ASSERT_TRUE(conn.keep_cnt > 0u);
    TEST_ASSERT_TRUE(dead <= 60000u);   /* a dead peer is found within a minute */
#else
    TEST_FAIL_MESSAGE("LWIP_TCP_KEEPALIVE is off: a silent peer is never found dead");
#endif
    err_cb(cb_arg, ERR_ABRT);           /* the probes went unanswered */
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(0, connect_calls);
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    connect_answers(ERR_OK);
}

/* Drop setUp's connection and wait out the backoff it begins. */
static void drop_and_wait(void)
{
    tfw_h5_wire_abort();
    abort_calls = 0u;
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
}

/* hal_eth_wire_tcp.c:155-156: tcp_new out of pcbs, the next try after the wait. */
static void test_no_pcb_from_lwip_dials_nothing_until_the_backoff(void)
{
    drop_and_wait();
    fail_new = 1u;
    tfw_h5_wire_dial_poll();
    fail_new = 0u;
    tfw_h5_wire_dial_poll();            /* the wait began at the first try */
    TEST_ASSERT_EQUAL_UINT(1, new_calls);
    TEST_ASSERT_EQUAL_UINT(0, connect_calls);

    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    connect_answers(ERR_OK);
    request_is_answered(11u);
}

/* hal_eth_wire_tcp.c:162-165: tcp_connect refused at once. */
static void test_a_connect_lwip_refuses_aborts_the_pcb_and_doubles_the_wait(void)
{
    drop_and_wait();
    fail_connect = ERR_MEM;
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    TEST_ASSERT_EQUAL_UINT(1, abort_calls);
    TEST_ASSERT_NULL(err_cb);           /* not armed on the freed pcb */
    fail_connect = ERR_OK;

    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(2, connect_calls);
    connect_answers(ERR_OK);
    request_is_answered(12u);
}

/* hal_eth_wire_tcp.c:38-39: the wait doubles up to REDIAL_MAX_MS, no further. */
static void test_the_redial_wait_stops_doubling_at_its_cap(void)
{
    static const uint32_t waits[] = { 1000u, 2000u, 4000u, 8000u, 16000u, 16000u };

    TEST_ASSERT_EQUAL_UINT32(1000u, TFW_H5_WIRE_REDIAL_MS);
    TEST_ASSERT_EQUAL_UINT32(16000u, TFW_H5_WIRE_REDIAL_MAX_MS);
    err_cb(cb_arg, ERR_RST);
    for (unsigned i = 0u; i < sizeof waits / sizeof waits[0]; i++) {
        tfw_mock_advance(waits[i] - 1u);
        tfw_h5_wire_dial_poll();
        TEST_ASSERT_EQUAL_UINT(i, connect_calls);
        tfw_mock_advance(1u);
        tfw_h5_wire_dial_poll();
        TEST_ASSERT_EQUAL_UINT(i + 1u, connect_calls);
        connect_answers(ERR_RST);
    }
}

/* hal_eth_wire_tcp.c:108-112: the connected callback reports an error. */
static void test_a_connected_callback_with_an_error_aborts_and_backs_off(void)
{
    drop_and_wait();
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    dialing = 0u;
    TEST_ASSERT_EQUAL(ERR_ABRT, connected_cb(cb_arg, &conn, ERR_CONN));
    TEST_ASSERT_EQUAL_UINT(1, abort_calls);
    TEST_ASSERT_NULL(err_cb);

    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(2, connect_calls);
    connect_answers(ERR_OK);
}

/* hal_eth_wire_tcp.c:104-105: a connected callback for a pcb not being dialled. */
static void test_a_connected_callback_for_another_pcb_is_refused(void)
{
    struct tcp_pcb other;

    memset(&other, 0, sizeof other);
    TEST_ASSERT_EQUAL(ERR_VAL, connected_cb(cb_arg, &other, ERR_OK));
    TEST_ASSERT_EQUAL_UINT(0, abort_calls);
    request_is_answered(13u);
}

/* hal_eth_wire_tcp.c:88-90: data on a pcb that is not the connection. */
static void test_data_on_another_pcb_is_dropped(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 14u, TFW_CMD_CAPS, 0, 0u);
    struct tcp_pcb other;
    struct pbuf p;

    TEST_ASSERT_NOT_EQUAL(0u, len);
    memset(&other, 0, sizeof other);
    memset(&p, 0, sizeof p);
    p.payload = wire;
    p.len     = len;
    p.tot_len = len;
    TEST_ASSERT_EQUAL(ERR_OK, recv_cb(cb_arg, &other, &p, ERR_OK));
    for (unsigned i = 0u; i < 4u; i++) { tfw_transport_poll(&t); }
    TEST_ASSERT_FALSE(answered(14u));
    TEST_ASSERT_EQUAL_UINT(0, close_calls);
    TEST_ASSERT_EQUAL_UINT(0, recved_total);
    request_is_answered(15u);
}

/* hal_eth_wire_tcp.c:78-85 with p == NULL, then 173-174: no connection. */
static void test_an_error_with_no_data_closes_and_later_sends_are_dropped(void)
{
    static const uint8_t bytes[3] = { 1u, 2u, 3u };

    TEST_ASSERT_EQUAL(ERR_OK, recv_cb(cb_arg, &conn, NULL, ERR_ABRT));
    TEST_ASSERT_EQUAL_UINT(1, close_calls);
    TEST_ASSERT_EQUAL_UINT16(3u, tfw_h5_wire_send(bytes, 3u));
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
}

/* hal_eth_wire_tcp.c:177-179: no send buffer, then a full send queue. */
static void test_a_full_send_window_sends_nothing(void)
{
    static const uint8_t bytes[4] = { 1u, 2u, 3u, 4u };

    conn.snd_buf = 0u;
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_h5_wire_send(bytes, 4u));
    conn.snd_buf = 4096u;
    conn.snd_queuelen = TCP_SND_QUEUELEN;
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_h5_wire_send(bytes, 4u));
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
}

/* hal_eth_wire_tcp.c:184-186: tcp_write refuses the bytes. */
static void test_a_write_lwip_refuses_sends_nothing(void)
{
    static const uint8_t bytes[4] = { 1u, 2u, 3u, 4u };

    fail_write = ERR_MEM;
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_h5_wire_send(bytes, 4u));
    TEST_ASSERT_EQUAL_UINT(0, sent_len);
}

/* hal_eth_wire_tcp.c:181-182: the send is clipped to tcp_sndbuf's room. */
static void test_a_send_larger_than_the_send_buffer_is_clipped_to_it(void)
{
    static const uint8_t bytes[4] = { 1u, 2u, 3u, 4u };

    conn.snd_buf = 2u;
    TEST_ASSERT_EQUAL_UINT16(2u, tfw_h5_wire_send(bytes, 4u));
    TEST_ASSERT_EQUAL_UINT(2, sent_len);
    TEST_ASSERT_EQUAL_HEX8(1u, sent[0]);
    TEST_ASSERT_EQUAL_HEX8(2u, sent[1]);
}

/* hal_eth_wire_tcp.c:92-95: each pbuf of a chain is pushed, in order. */
static void test_a_request_split_across_a_pbuf_chain_is_answered(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 16u, TFW_CMD_CAPS, 0, 0u);
    struct pbuf a, b;

    TEST_ASSERT_TRUE(len > 3u);
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.payload = wire;
    a.len     = 3u;
    a.tot_len = len;
    a.next    = &b;
    b.payload = wire + 3;
    b.len     = (u16_t)(len - 3u);
    b.tot_len = b.len;
    TEST_ASSERT_EQUAL(ERR_OK, recv_cb(cb_arg, &conn, &a, ERR_OK));
    for (unsigned i = 0u; i < 4u; i++) { tfw_transport_poll(&t); }
    TEST_ASSERT_TRUE(answered(16u));
    TEST_ASSERT_EQUAL_UINT(len, recved_total);
    TEST_ASSERT_EQUAL_UINT(0, close_calls);
}

/* hal_eth_wire_tcp.c:221-222: tcp_close fails, so the pcb is aborted. */
static void test_a_close_with_no_memory_for_the_fin_resets(void)
{
    fail_close = ERR_MEM;
    tcp_input(NULL, 0u, 1u);
    tfw_transport_poll(&t);
    TEST_ASSERT_EQUAL_UINT(1, close_calls);
    TEST_ASSERT_EQUAL_UINT(1, abort_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_device_dials_the_block_endpoint_and_binds_nothing);
    RUN_TEST(test_a_refused_dial_is_retried_on_the_block_read_again);
    RUN_TEST(test_a_missing_block_dials_nothing);
    RUN_TEST(test_a_request_sent_with_fin_is_answered_before_the_close);
    RUN_TEST(test_a_bare_fin_closes_on_the_next_poll);
    RUN_TEST(test_an_error_closes_at_once);
    RUN_TEST(test_a_redial_reconciles_the_netif_with_the_block_it_dials);
    RUN_TEST(test_no_dial_while_the_netif_is_not_ready_on_the_block);
    RUN_TEST(test_a_silent_peer_is_found_dead_by_keepalive_and_redialled);
    RUN_TEST(test_no_pcb_from_lwip_dials_nothing_until_the_backoff);
    RUN_TEST(test_a_connect_lwip_refuses_aborts_the_pcb_and_doubles_the_wait);
    RUN_TEST(test_the_redial_wait_stops_doubling_at_its_cap);
    RUN_TEST(test_a_connected_callback_with_an_error_aborts_and_backs_off);
    RUN_TEST(test_a_connected_callback_for_another_pcb_is_refused);
    RUN_TEST(test_data_on_another_pcb_is_dropped);
    RUN_TEST(test_an_error_with_no_data_closes_and_later_sends_are_dropped);
    RUN_TEST(test_a_full_send_window_sends_nothing);
    RUN_TEST(test_a_write_lwip_refuses_sends_nothing);
    RUN_TEST(test_a_send_larger_than_the_send_buffer_is_clipped_to_it);
    RUN_TEST(test_a_request_split_across_a_pbuf_chain_is_answered);
    RUN_TEST(test_a_close_with_no_memory_for_the_fin_resets);
    return UNITY_END();
}
