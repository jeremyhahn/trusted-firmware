/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Ethernet link with the DHCP client in it: src/proto/link_eth.c over
 * src/net/dhcp.c, where test_link_eth links src/net/dhcp_none.c. This is the
 * link a DHCP=1 image runs. The block comes from the mock EEPROM through
 * tfw_netcfg_load(), the clock is the mock HAL's, and the suite fakes both
 * sockets at include/tfw/eth.h: the TCP connection the link holds, and the
 * UDP socket the client asks on, whose far end is the suite as the server.
 *
 * What is pinned is RFC 2131 §4.4.5 as the link sees it: up to T1 the lease
 * is left alone; at T1 the client renews beside the connection, which keeps
 * its address; at T2 any server may rebind it; a DHCPNAK, or the lease
 * running out, takes the address away, so the link drops the connection and
 * asks again from no address.
 */

#include <string.h>
#include "unity.h"
#include "crc16.h"
#include "hal_mock.h"
#include "tfw/eth.h"
#include "tfw/link.h"
#include "tfw/hal.h"

#define MSG_LEN   300u               /* the client pads to the BOOTP minimum */
#define STEP_MS   10u
#define LONGEST_BACKOFF_MS 64000u

#define LEASE_MS  (3600u * 1000u)    /* the lease the server grants */
#define T1_MS     (LEASE_MS / 2u)
#define T2_MS     (LEASE_MS - LEASE_MS / 8u)

#define DHCPDISCOVER 1u
#define DHCPOFFER    2u
#define DHCPREQUEST  3u
#define DHCPACK      5u
#define DHCPNAK      6u

static const uint8_t mac[6]     = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u };
static const uint8_t srv_a[4]   = { 192u, 0u, 2u, 1u };
static const uint8_t srv_b[4]   = { 192u, 0u, 2u, 2u };
static const uint8_t offered[4] = { 192u, 0u, 2u, 77u };
static const uint8_t netmask[4] = { 255u, 255u, 255u, 0u };
static const uint8_t gw[4]      = { 192u, 0u, 2u, 254u };
static const uint8_t bcast[4]   = { 255u, 255u, 255u, 255u };
static const uint8_t none[4]    = { 0u, 0u, 0u, 0u };

/* Both sockets, as the link and the client see them. */
static struct {
    uint8_t      state;              /* the connection's, TFW_ETH_* */
    unsigned     starts, closes;
    tfw_netcfg_t cfg;                /* what tfw_eth_start() was last given */

    uint8_t      udp;                /* 0 closed, 1 opened, 2 opened beside */
    unsigned     opens;
    tfw_netcfg_t open_cfg;           /* what tfw_eth_udp_open() was given */
    uint8_t      out[MSG_LEN];       /* the datagram being written */
    uint8_t      msg[MSG_LEN];       /* the last DHCP message sent */
    uint8_t      dst[4];             /* where it went */
    unsigned     dgrams;             /* DHCP messages sent */
    unsigned     probes;             /* datagrams to port 9 */
    uint8_t      sent;               /* what tfw_eth_udp_sent() says */
    uint8_t      in[8u + MSG_LEN];   /* one reply, behind the chip's header */
    uint16_t     in_len, in_pos;
} eth;

tfw_status_t tfw_eth_start(const tfw_netcfg_t *cfg, uint16_t local_port)
{
    (void)local_port;
    eth.starts++;
    eth.cfg   = *cfg;
    eth.state = TFW_ETH_BUSY;
    return TFW_OK;
}

uint8_t tfw_eth_state(void) { return eth.state; }
uint8_t tfw_eth_rx(uint8_t *out) { (void)out; return 0u; }
uint8_t tfw_eth_tx(uint8_t byte) { (void)byte; return 1u; }
void    tfw_eth_send(void) {}

void tfw_eth_close(void)
{
    eth.closes++;
    eth.state = TFW_ETH_DOWN;
}

tfw_status_t tfw_eth_udp_open(const tfw_netcfg_t *cfg, uint16_t local_port)
{
    TEST_ASSERT_EQUAL_UINT16(68u, local_port);
    eth.udp      = 1u;
    eth.open_cfg = *cfg;
    eth.opens++;
    return TFW_OK;
}

tfw_status_t tfw_eth_udp_beside(uint16_t local_port)
{
    TEST_ASSERT_EQUAL_UINT16(68u, local_port);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, eth.state);    /* beside a connection */
    eth.udp = 2u;
    return TFW_OK;
}

void tfw_eth_udp_close(void)
{
    eth.udp    = 0u;
    eth.in_len = eth.in_pos = 0u;
}

void tfw_eth_udp_put(uint16_t at, uint8_t byte)
{
    TEST_ASSERT_TRUE(at < MSG_LEN);
    eth.out[at] = byte;
}

uint8_t tfw_eth_udp_send(uint16_t len, const uint8_t *ip, uint16_t port)
{
    TEST_ASSERT_NOT_EQUAL_UINT8(0u, eth.udp);
    if (port == 9u) {                /* the probe of the offered address */
        TEST_ASSERT_EQUAL_UINT16(1u, len);
        eth.probes++;
        eth.sent = TFW_ETH_SENT_NOARP;                  /* nobody holds it */
        return 1u;
    }
    TEST_ASSERT_EQUAL_UINT16(67u, port);
    TEST_ASSERT_EQUAL_UINT16(MSG_LEN, len);
    memcpy(eth.msg, eth.out, MSG_LEN);
    memcpy(eth.dst, ip, 4u);
    eth.dgrams++;
    eth.sent = TFW_ETH_SENT_OK;
    return 1u;
}

uint8_t  tfw_eth_udp_sent(void) { return eth.sent; }
uint16_t tfw_eth_udp_avail(void) { return (uint16_t)(eth.in_len - eth.in_pos); }

uint8_t tfw_eth_udp_get(uint16_t at)
{
    TEST_ASSERT_TRUE(eth.in_pos + at < eth.in_len);
    return eth.in[eth.in_pos + at];
}

void tfw_eth_udp_done(uint16_t len)
{
    eth.in_pos = (uint16_t)(eth.in_pos + len);
    if (eth.in_pos >= eth.in_len) {
        eth.in_len = eth.in_pos = 0u;
    }
}

static tfw_ms_t clock_ms;

/* A block that asks for DHCP: no address of its own, the endpoint to dial. */
static void provision(void)
{
    uint8_t  b[TFW_NETCFG_LEN];
    uint16_t crc;

    memset(b, 0, sizeof b);
    memcpy(b, "TFNC", 4u);
    b[4] = 0x01u;
    b[5] = TFW_NETCFG_DHCP;
    memcpy(&b[6], mac, 6u);
    b[24] = 192u; b[25] = 0u; b[26] = 2u; b[27] = 20u;
    b[28] = 0x1Bu; b[29] = 0x6Cu;                       /* port 7020 */
    crc = tfw_crc16(b, 30u);
    b[30] = (uint8_t)crc;
    b[31] = (uint8_t)(crc >> 8);
    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], b, sizeof b);
}

void setUp(void)
{
    tfw_netcfg_t plain;

    tfw_mock_reset();
    clock_ms += 100000000u;
    tfw_mock_set_millis(clock_ms);
    memset(&eth, 0, sizeof eth);
    /* The client keeps its lease across polls; a static block forgets it. */
    memset(&plain, 0, sizeof plain);
    (void)tfw_dhcp_poll(&plain);
    tfw_link_poll();                       /* drop what the last test left */
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    memset(&eth, 0, sizeof eth);
    provision();
}
void tearDown(void) {}

static void put4(uint8_t *p, const uint8_t *v) { memcpy(p, v, 4u); }

/* The server's reply to the last message, from server, with a lease of
 * lease_s seconds and the mask and router when lease_s is nonzero. */
static void hand_up(uint8_t type, const uint8_t *server, uint32_t lease_s)
{
    uint8_t *m = eth.in + 8u;
    uint16_t n = 240u;

    TEST_ASSERT_EQUAL_UINT16(0u, eth.in_len);
    memset(eth.in, 0, sizeof eth.in);
    m[0] = 2u;                                          /* BOOTREPLY */
    m[1] = 1u;
    m[2] = 6u;
    memcpy(m + 4, eth.msg + 4, 4u);                     /* xid */
    if (type != DHCPNAK) {
        put4(m + 16, offered);                          /* yiaddr */
    }
    memcpy(m + 28, mac, 6u);
    m[236] = 99u; m[237] = 130u; m[238] = 83u; m[239] = 99u;
    m[n++] = 53u; m[n++] = 1u; m[n++] = type;
    m[n++] = 54u; m[n++] = 4u; put4(m + n, server); n += 4u;
    if (lease_s) {
        m[n++] = 51u; m[n++] = 4u;
        m[n++] = (uint8_t)(lease_s >> 24); m[n++] = (uint8_t)(lease_s >> 16);
        m[n++] = (uint8_t)(lease_s >> 8);  m[n++] = (uint8_t)lease_s;
        m[n++] = 1u; m[n++] = 4u; put4(m + n, netmask); n += 4u;
        m[n++] = 3u; m[n++] = 4u; put4(m + n, gw); n += 4u;
    }
    m[n] = 255u;
    put4(eth.in, server);
    eth.in[4] = 0u;
    eth.in[5] = 67u;
    eth.in[6] = (uint8_t)(MSG_LEN >> 8);
    eth.in[7] = (uint8_t)MSG_LEN;
    eth.in_len = (uint16_t)(8u + MSG_LEN);
    eth.in_pos = 0u;
}

/* Polls the link on the mock clock in STEP_MS steps until the client sends a
 * DHCP message or limit_ms passes; 1 if it sent. */
static uint8_t poll_until_sent(tfw_ms_t limit_ms)
{
    unsigned dgrams = eth.dgrams;
    tfw_ms_t waited;

    for (waited = 0u; waited <= limit_ms; waited += STEP_MS) {
        tfw_link_poll();
        if (eth.dgrams != dgrams) {
            return 1u;
        }
        tfw_mock_advance(STEP_MS);
    }
    return 0u;
}

/* The last message: its type, where it went, its BROADCAST flag and ciaddr. */
static uint8_t sent_as(const uint8_t *dst, uint8_t flags, const uint8_t *ci)
{
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dst, eth.dst, 4u);
    TEST_ASSERT_EQUAL_UINT8(1u, eth.msg[0]);            /* BOOTREQUEST */
    TEST_ASSERT_EQUAL_UINT8(flags, eth.msg[10]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ci, eth.msg + 12, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, eth.msg + 28, 6u);
    TEST_ASSERT_EQUAL_UINT8(53u, eth.msg[240]);
    return eth.msg[242];
}

/* The connection the link holds, and the address it holds it from. */
static void assert_connected_from_the_lease(unsigned starts)
{
    TEST_ASSERT_EQUAL_UINT(starts, eth.starts);
    TEST_ASSERT_EQUAL_UINT(0u, eth.closes);
    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_UP, eth.state);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, eth.cfg.ip, 4u);
}

/* From a block that asks for DHCP to a connection from the lease; returns
 * the time the lease counts from, its DHCPREQUEST (§4.4.1). */
static tfw_ms_t lease_up(void)
{
    tfw_ms_t asked;

    TEST_ASSERT_EQUAL_UINT8(1u, poll_until_sent(LONGEST_BACKOFF_MS + 11000u));
    TEST_ASSERT_EQUAL_UINT8(DHCPDISCOVER, sent_as(bcast, 0x80u, none));
    TEST_ASSERT_EQUAL_UINT8(1u, eth.udp);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, eth.open_cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT(0u, eth.starts);             /* no address: no dial */

    hand_up(DHCPOFFER, srv_a, 0u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT8(DHCPREQUEST, sent_as(bcast, 0x80u, none));
    asked = tfw_hal_millis();
    TEST_ASSERT_EQUAL_UINT(0u, eth.starts);

    hand_up(DHCPACK, srv_a, LEASE_MS / 1000u);
    tfw_link_poll();                                    /* probes the address */
    TEST_ASSERT_EQUAL_UINT(1u, eth.probes);
    TEST_ASSERT_EQUAL_UINT(0u, eth.starts);
    tfw_link_poll();                                    /* free: bound, dials */
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, eth.cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(netmask, eth.cfg.mask, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(gw, eth.cfg.gateway, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, eth.udp);               /* given back */

    eth.state = TFW_ETH_UP;
    tfw_link_poll();
    assert_connected_from_the_lease(1u);
    return asked;
}

static void at(tfw_ms_t t)
{
    tfw_mock_set_millis(t);
    tfw_link_poll();
}

/* The link asks again from INIT: the socket opened from no address, and the
 * first DHCPDISCOVER on its way. */
static void asks_again_from_no_address(void)
{
    unsigned opens = eth.opens;

    TEST_ASSERT_EQUAL_UINT8(TFW_ETH_DOWN, eth.state);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(opens + 1u, eth.opens);
    TEST_ASSERT_EQUAL_UINT8(1u, eth.udp);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, eth.open_cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8(1u, poll_until_sent(11000u));
    TEST_ASSERT_EQUAL_UINT8(DHCPDISCOVER, sent_as(bcast, 0x80u, none));
}

static void test_before_t1_the_link_holds_the_lease_and_asks_nothing(void)
{
    tfw_ms_t asked  = lease_up();
    unsigned dgrams = eth.dgrams;

    at(asked + 1000u);
    at(asked + T1_MS - 1u);
    TEST_ASSERT_EQUAL_UINT(dgrams, eth.dgrams);
    TEST_ASSERT_EQUAL_UINT8(0u, eth.udp);
    assert_connected_from_the_lease(1u);
}

static void test_at_t1_the_link_renews_beside_its_connection_and_keeps_its_address(void)
{
    tfw_ms_t asked = lease_up();

    at(asked + T1_MS);
    /* DHCPREQUEST to the server that granted it, ciaddr the lease (§4.3.2). */
    TEST_ASSERT_EQUAL_UINT8(DHCPREQUEST, sent_as(srv_a, 0x00u, offered));
    TEST_ASSERT_EQUAL_UINT8(255u, eth.msg[247]);        /* no 50, no 54 */
    TEST_ASSERT_EQUAL_UINT8(2u, eth.udp);
    assert_connected_from_the_lease(1u);

    hand_up(DHCPACK, srv_a, LEASE_MS / 1000u);
    at(asked + T1_MS + 2000u);
    TEST_ASSERT_EQUAL_UINT8(0u, eth.udp);
    assert_connected_from_the_lease(1u);

    /* The new lease counts from T1: its T1 falls where the first lease ends,
     * and the address outlives the first lease. */
    at(asked + LEASE_MS - 1u);
    TEST_ASSERT_EQUAL_UINT8(0u, eth.udp);
    at(asked + LEASE_MS);
    TEST_ASSERT_EQUAL_UINT8(DHCPREQUEST, sent_as(srv_a, 0x00u, offered));
    assert_connected_from_the_lease(1u);
}

static void test_at_t2_any_server_rebinds_and_the_link_keeps_its_address(void)
{
    tfw_ms_t asked = lease_up();

    at(asked + T1_MS);                                  /* no answer */
    TEST_ASSERT_EQUAL_UINT8(DHCPREQUEST, sent_as(srv_a, 0x00u, offered));
    at(asked + T2_MS);
    TEST_ASSERT_EQUAL_UINT8(DHCPREQUEST, sent_as(bcast, 0x00u, offered));
    assert_connected_from_the_lease(1u);

    hand_up(DHCPACK, srv_b, LEASE_MS / 1000u);
    at(asked + T2_MS + 1000u);
    TEST_ASSERT_EQUAL_UINT8(0u, eth.udp);
    assert_connected_from_the_lease(1u);

    /* From T2, and renewed from the server that rebound it. */
    at(asked + T2_MS + T1_MS - 1u);
    TEST_ASSERT_EQUAL_UINT8(0u, eth.udp);
    at(asked + T2_MS + T1_MS);
    TEST_ASSERT_EQUAL_UINT8(DHCPREQUEST, sent_as(srv_b, 0x00u, offered));
    assert_connected_from_the_lease(1u);
}

static void test_a_lease_that_runs_out_drops_the_connection_and_asks_again(void)
{
    tfw_ms_t asked = lease_up();

    at(asked + T1_MS);
    at(asked + T2_MS);
    TEST_ASSERT_EQUAL_UINT8(DHCPREQUEST, sent_as(bcast, 0x00u, offered));
    /* No server answers: the address is held to the last millisecond... */
    at(asked + LEASE_MS - 1u);
    assert_connected_from_the_lease(1u);
    /* ...and given up at the lease's end (§4.4.5): the connection goes. */
    at(asked + LEASE_MS);
    TEST_ASSERT_EQUAL_UINT(1u, eth.closes);
    TEST_ASSERT_EQUAL_UINT8(0u, eth.udp);
    asks_again_from_no_address();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);             /* not dialled again */
}

static void test_a_nak_while_renewing_drops_the_connection_at_once(void)
{
    tfw_ms_t asked = lease_up();

    at(asked + T1_MS);
    hand_up(DHCPNAK, srv_b, 0u);                        /* not our server */
    at(asked + T1_MS + 1000u);
    assert_connected_from_the_lease(1u);
    hand_up(DHCPNAK, srv_a, 0u);
    at(asked + T1_MS + 2000u);
    TEST_ASSERT_EQUAL_UINT(1u, eth.closes);
    asks_again_from_no_address();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
}

static void test_a_connection_lost_while_renewing_redials_from_the_lease_and_renews(void)
{
    tfw_ms_t asked = lease_up();
    unsigned dgrams;

    at(asked + T1_MS);
    eth.state = TFW_ETH_DOWN;                           /* the peer went */
    at(asked + T1_MS + 1000u);
    TEST_ASSERT_EQUAL_UINT(1u, eth.closes);
    tfw_mock_advance(LONGEST_BACKOFF_MS);
    dgrams = eth.dgrams;
    tfw_link_poll();                                    /* the lease still runs */
    TEST_ASSERT_EQUAL_UINT(2u, eth.starts);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, eth.cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, eth.udp);
    TEST_ASSERT_EQUAL_UINT(dgrams, eth.dgrams);
    eth.state = TFW_ETH_UP;
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT8(DHCPREQUEST, sent_as(srv_a, 0x00u, offered));
    TEST_ASSERT_EQUAL_UINT8(2u, eth.udp);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_before_t1_the_link_holds_the_lease_and_asks_nothing);
    RUN_TEST(test_at_t1_the_link_renews_beside_its_connection_and_keeps_its_address);
    RUN_TEST(test_at_t2_any_server_rebinds_and_the_link_keeps_its_address);
    RUN_TEST(test_a_lease_that_runs_out_drops_the_connection_and_asks_again);
    RUN_TEST(test_a_nak_while_renewing_drops_the_connection_at_once);
    RUN_TEST(test_a_connection_lost_while_renewing_redials_from_the_lease_and_renews);
    return UNITY_END();
}
