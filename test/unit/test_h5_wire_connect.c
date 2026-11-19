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

struct tcp_pcb *tcp_new(void)
{
    new_calls++;
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

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_transport_init(&t);
    tfw_framer_init(&rxf);
    put_block(4000u);
    tfw_h5_wire_abort();                /* a connection a test left open */
    if (dialing) {
        connect_answers(ERR_ABRT);
    }
    tfw_mock_advance(TFW_H5_WIRE_REDIAL_MAX_MS);
    new_calls = connect_calls = bind_calls = listen_calls = 0u;
    tfw_h5_wire_dial_poll();
    TEST_ASSERT_EQUAL_UINT(1, connect_calls);
    connect_answers(ERR_OK);
    TEST_ASSERT_NOT_NULL(recv_cb);
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_device_dials_the_block_endpoint_and_binds_nothing);
    RUN_TEST(test_a_refused_dial_is_retried_on_the_block_read_again);
    RUN_TEST(test_a_missing_block_dials_nothing);
    RUN_TEST(test_a_request_sent_with_fin_is_answered_before_the_close);
    RUN_TEST(test_a_bare_fin_closes_on_the_next_poll);
    RUN_TEST(test_an_error_closes_at_once);
    return UNITY_END();
}
