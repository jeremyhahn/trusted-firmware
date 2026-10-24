/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The DHCP client -- src/net/dhcp.c over src/net/w5x00.c and one chip file --
 * against the software W5100 / W5500 in test/model/w5x00_model.c. The test is
 * the server: it reads each datagram the client sends off the model's wire and
 * hands replies up behind the 8-byte header the chip prepends. The clock is
 * the suite's. Built twice, as test_w5x00.c is: W5X_TEST_CHIP 1 and 2.
 *
 * The client runs on socket 1, so the model's peer calls act on it; socket 0
 * is the link's connection, which renewal must leave as it is.
 *
 * What is pinned is RFC 2131's: the message fields of Table 5, the 1 to 10 s
 * wait before the first DHCPDISCOVER (§4.4.1), the 4 s doubling to 64 s
 * retransmission randomised by +-1 s (§4.1), the replies a client must discard,
 * DHCPNAK, the lease counted from the DHCPREQUEST (§4.4.1), the probe of the
 * offered address and DHCPDECLINE with its 10 s wait (§2.2, §3.1, §4.4.1), and
 * renewal at T1 and rebinding at T2 with requests at least 60 s apart (§4.4.5).
 */

#include <string.h>
#include "unity.h"
#include "tfw/eth.h"
#include "tfw/hal.h"
#include "tfw/board.h"
#include "w5x00_model.h"

#define CHIP   ((uint8_t)W5X_TEST_CHIP)
#define OTHER  ((uint8_t)(3u - W5X_TEST_CHIP))

/* Socket register offsets, from the datasheets, for inspecting the model. */
#define S_MR     0x00u
#define S_SR     0x03u
#define S_PORT   0x04u
#define S_DIPR   0x0Cu
#define S_DPORT  0x10u

#define MSG_LEN  300u                /* the client pads to the BOOTP minimum */
#define STEP_MS  10u

static const uint8_t mac[6]     = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u };
static const uint8_t srv_a[4]   = { 192u, 0u, 2u, 1u };
static const uint8_t srv_b[4]   = { 192u, 0u, 2u, 2u };
static const uint8_t offered[4] = { 192u, 0u, 2u, 77u };
static const uint8_t netmask[4] = { 255u, 255u, 255u, 0u };
static const uint8_t gw[4]      = { 192u, 0u, 2u, 254u };
static const uint8_t bcast[4]   = { 255u, 255u, 255u, 255u };
static const uint8_t none[4]    = { 0u, 0u, 0u, 0u };

#define LEASE_MS  (3600u * 1000u)    /* reply(5)'s lease */
#define T1_MS     (LEASE_MS / 2u)
#define T2_MS     (LEASE_MS - LEASE_MS / 8u)

static tfw_ms_t     now_ms;
static tfw_netcfg_t cfg;
static uint8_t      msg[MSG_LEN];    /* the last datagram the client sent */
static uint8_t      xid[4];

tfw_ms_t tfw_hal_millis(void) { return now_ms; }

/* A network block that asks for DHCP, as tfw_netcfg_load() hands it over. */
static void block(uint8_t flags)
{
    static const uint8_t own_ip[4] = { 198u, 51u, 100u, 10u };

    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.mac, mac, 6u);
    memcpy(cfg.ip, own_ip, 4u);
    memcpy(cfg.mask, netmask, 4u);
    memcpy(cfg.gateway, own_ip, 4u);
    cfg.peer[0] = 192u;
    cfg.peer[1] = 0u;
    cfg.peer[2] = 2u;
    cfg.peer[3] = 20u;
    cfg.port  = 7020u;
    cfg.flags = flags;
}

void setUp(void)
{
    w5x_model_reset(CHIP);
    now_ms = 100000u;
    block(TFW_NETCFG_DHCP);
    /* The client keeps its state across polls; a static block resets it. */
    {
        tfw_netcfg_t plain = cfg;

        plain.flags = 0u;
        (void)tfw_dhcp_poll(&plain);
    }
    w5x_model_socket(1u);
    w5x_model_arp_silent(offered);   /* the offered address is free */
}
void tearDown(void) {}

typedef struct {
    uint8_t        op;
    uint8_t        type;
    const uint8_t *server;           /* option 54, or none */
    uint32_t       lease;            /* option 51 when nonzero */
    uint8_t        mask;             /* option 1 */
    uint8_t        router;           /* option 3, two routers */
    uint8_t        overload;         /* option 52 */
    uint8_t        end;              /* option 255 */
    uint16_t       sport;
    uint8_t        bad_xid, bad_mac, bad_cookie;
    uint16_t       claim;            /* header length beyond what is sent */
} reply_t;

static reply_t reply(uint8_t type)
{
    reply_t r;

    memset(&r, 0, sizeof r);
    r.op     = 2u;
    r.type   = type;
    r.server = srv_a;
    r.lease  = (type == 5u) ? 3600u : 0u;
    r.mask   = 1u;
    r.router = 1u;
    r.end    = 1u;
    r.sport  = 67u;
    return r;
}

static void put4(uint8_t *p, const uint8_t *v) { memcpy(p, v, 4u); }

/* Puts r on the model's receive ring as the chip would: header, then message. */
static void hand_up(const reply_t *r)
{
    uint8_t  d[8u + MSG_LEN];
    uint8_t *m = d + 8u;
    uint16_t n = 240u, len;

    memset(d, 0, sizeof d);
    m[0] = r->op;
    m[1] = 1u;
    m[2] = 6u;
    memcpy(m + 4, xid, 4u);
    m[4] = (uint8_t)(m[4] ^ r->bad_xid);
    put4(m + 16, offered);
    put4(m + 20, srv_a);
    memcpy(m + 28, mac, 6u);
    m[33] = (uint8_t)(m[33] ^ r->bad_mac);
    m[236] = 99u; m[237] = 130u; m[238] = 83u; m[239] = (uint8_t)(99u ^ r->bad_cookie);
    m[n++] = 53u; m[n++] = 1u; m[n++] = r->type;
    if (r->overload) {
        m[n++] = 52u; m[n++] = 1u; m[n++] = 3u;
    }
    if (r->server) {
        m[n++] = 54u; m[n++] = 4u; put4(m + n, r->server); n += 4u;
    }
    if (r->lease) {
        m[n++] = 51u; m[n++] = 4u;
        m[n++] = (uint8_t)(r->lease >> 24); m[n++] = (uint8_t)(r->lease >> 16);
        m[n++] = (uint8_t)(r->lease >> 8);  m[n++] = (uint8_t)r->lease;
    }
    if (r->mask) {
        m[n++] = 1u; m[n++] = 4u; put4(m + n, netmask); n += 4u;
    }
    m[n++] = 0u;                                            /* a pad between */
    if (r->router) {
        m[n++] = 3u; m[n++] = 8u; put4(m + n, gw); n += 4u; put4(m + n, srv_a); n += 4u;
    }
    if (r->end) {
        m[n++] = 255u;
    }
    n = MSG_LEN;
    put4(d, srv_a);
    d[4] = (uint8_t)(r->sport >> 8);
    d[5] = (uint8_t)r->sport;
    len  = (uint16_t)(n + r->claim);
    d[6] = (uint8_t)(len >> 8);
    d[7] = (uint8_t)len;
    w5x_model_peer_send(d, (uint16_t)(8u + n));
}

/* Polls on the clock in STEP_MS steps until the client sends, answers other
 * than BUSY, or limit_ms passes. Returns the last answer. */
static tfw_status_t run(tfw_ms_t limit_ms)
{
    tfw_ms_t     end   = now_ms + limit_ms;
    uint8_t      sends = w5x_model_sends();
    tfw_status_t st;

    for (;;) {
        st = tfw_dhcp_poll(&cfg);
        if (st != TFW_ERR_BUSY || w5x_model_sends() != sends ||
            (int32_t)(now_ms - end) >= 0) {
            return st;
        }
        now_ms += STEP_MS;
    }
}

/* Takes the one datagram the client sent off the wire, checks where it went
 * and the fields every message carries (Table 5), and returns its type. dst is
 * where it went, flags its BROADCAST byte and ci its ciaddr. */
static uint8_t sent_as(const uint8_t *dst, uint8_t flags, const uint8_t *ci)
{
    uint8_t buf[2u * MSG_LEN];
    uint8_t i;

    TEST_ASSERT_EQUAL_UINT16(MSG_LEN, w5x_model_peer_recv(buf, sizeof buf));
    memcpy(msg, buf, MSG_LEN);
    for (i = 0u; i < 4u; i++) {
        TEST_ASSERT_EQUAL_UINT8(dst[i], w5x_model_sock((uint16_t)(S_DIPR + i)));
    }
    TEST_ASSERT_EQUAL_UINT16(67u, w5x_model_sock16(S_DPORT));
    TEST_ASSERT_EQUAL_UINT8(1u, msg[0]);                    /* BOOTREQUEST */
    TEST_ASSERT_EQUAL_UINT8(1u, msg[1]);                    /* Ethernet */
    TEST_ASSERT_EQUAL_UINT8(6u, msg[2]);
    TEST_ASSERT_EQUAL_UINT8(0u, msg[3]);
    TEST_ASSERT_EQUAL_UINT8(flags, msg[10]);                /* BROADCAST */
    TEST_ASSERT_EQUAL_UINT8(0u, msg[11]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ci, msg + 12, 4u);        /* ciaddr */
    for (i = 16u; i < 28u; i++) {
        TEST_ASSERT_EQUAL_UINT8(0u, msg[i]);                /* yiaddr .. giaddr */
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, msg + 28, 6u);
    TEST_ASSERT_EQUAL_UINT8(99u,  msg[236]);
    TEST_ASSERT_EQUAL_UINT8(130u, msg[237]);
    TEST_ASSERT_EQUAL_UINT8(83u,  msg[238]);
    TEST_ASSERT_EQUAL_UINT8(99u,  msg[239]);
    TEST_ASSERT_EQUAL_UINT8(53u, msg[240]);
    TEST_ASSERT_EQUAL_UINT8(1u,  msg[241]);
    if (msg[242] != 4u) {
        /* The same parameter request list in every message (§4.3.1), and
         * none in DHCPDECLINE (Table 5). */
        TEST_ASSERT_EQUAL_UINT8(55u, msg[243]);
        TEST_ASSERT_EQUAL_UINT8(2u,  msg[244]);
        TEST_ASSERT_EQUAL_UINT8(1u,  msg[245]);
        TEST_ASSERT_EQUAL_UINT8(3u,  msg[246]);
    }
    return msg[242];
}

static uint8_t sent(void)
{
    return sent_as(bcast, 0x80u, none);
}

static void discover(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, run(11000u));
    TEST_ASSERT_EQUAL_UINT8(1u, sent());                    /* DHCPDISCOVER */
    TEST_ASSERT_EQUAL_UINT8(255u, msg[247]);
    memcpy(xid, msg + 4, 4u);
}

static void offer_and_request(void)
{
    reply_t r = reply(2u);

    discover();
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8(3u, sent());                    /* DHCPREQUEST */
}

/* Hands up a DHCPACK the client takes: the poll that reads it probes the
 * address, and the next one says what the probe found. */
static tfw_status_t acked(const reply_t *r)
{
    hand_up(r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    return tfw_dhcp_poll(&cfg);
}

/* Bound, and the time the lease counts from. */
static tfw_ms_t bind(void)
{
    reply_t  r = reply(5u);
    tfw_ms_t asked;

    offer_and_request();
    asked = now_ms;
    TEST_ASSERT_EQUAL(TFW_OK, acked(&r));
    return asked;
}

/* The link connects from the lease on socket 0, as link_eth.c does. */
static void connect_link(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_eth_start(&cfg, 49152u));
    w5x_model_socket(0u);
    w5x_model_peer_accept();
    w5x_model_socket(1u);
}

static uint8_t link_sr(void)
{
    uint8_t sr;

    w5x_model_socket(0u);
    sr = w5x_model_sock(S_SR);
    w5x_model_socket(1u);
    return sr;
}

/* A renewal sent at T1 or after: unicast to the server, ciaddr ours. */
static void renewal_sent(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT8(3u, sent_as(srv_a, 0x00u, offered));
    TEST_ASSERT_EQUAL_UINT8(255u, msg[247]);                /* no 50, no 54 */
    memcpy(xid, msg + 4, 4u);
}

static void test_a_static_block_is_used_as_it_is_and_the_chip_untouched(void)
{
    uint32_t writes = w5x_model_writes();

    block(0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8(198u, cfg.ip[0]);
    TEST_ASSERT_EQUAL_UINT32(writes, w5x_model_writes());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT32(writes, w5x_model_writes());
}

static void test_the_socket_opens_as_udp_on_port_68_with_no_address(void)
{
    uint8_t i;

    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8(0x02u, w5x_model_sock(S_MR));          /* UDP */
    TEST_ASSERT_EQUAL_UINT8(0x22u, w5x_model_sock(S_SR));          /* SOCK_UDP */
    TEST_ASSERT_EQUAL_UINT16(68u, w5x_model_sock16(S_PORT));
    for (i = 0u; i < 4u; i++) {
        TEST_ASSERT_EQUAL_UINT8(0u, w5x_model_common((uint16_t)(0x01u + i)));  /* GAR */
        TEST_ASSERT_EQUAL_UINT8(0u, w5x_model_common((uint16_t)(0x05u + i)));  /* SUBR */
        TEST_ASSERT_EQUAL_UINT8(0u, w5x_model_common((uint16_t)(0x0Fu + i)));  /* SIPR */
    }
    for (i = 0u; i < 6u; i++) {
        TEST_ASSERT_EQUAL_UINT8(mac[i], w5x_model_common((uint16_t)(0x09u + i)));
    }
}

static void test_the_first_discover_waits_between_1_and_10_s(void)
{
    tfw_ms_t start = now_ms;

    discover();
    TEST_ASSERT_TRUE(now_ms - start >= 1000u);
    TEST_ASSERT_TRUE(now_ms - start <= 10000u + STEP_MS);
}

static void test_the_other_chip_is_refused_and_no_address_is_taken(void)
{
    w5x_model_reset(OTHER);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_NOT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8(0u, w5x_model_sends());
}

static void test_an_offer_is_answered_with_a_request_naming_its_server_and_address(void)
{
    offer_and_request();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(xid, msg + 4, 4u);        /* the offer's xid */
    TEST_ASSERT_EQUAL_UINT8(50u, msg[247]);                 /* requested IP */
    TEST_ASSERT_EQUAL_UINT8(4u,  msg[248]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, msg + 249, 4u);
    TEST_ASSERT_EQUAL_UINT8(54u, msg[253]);                 /* server id */
    TEST_ASSERT_EQUAL_UINT8(4u,  msg[254]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(srv_a, msg + 255, 4u);
    TEST_ASSERT_EQUAL_UINT8(255u, msg[259]);
}

static void test_an_ack_hands_the_link_the_lease_and_gives_the_socket_back(void)
{
    bind();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(netmask, cfg.mask, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(gw, cfg.gateway, 4u);     /* the first router */
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));   /* CLOSED */
}

static void test_a_bound_client_answers_from_its_lease_without_the_chip(void)
{
    uint32_t writes;

    bind();
    writes = w5x_model_writes();
    block(TFW_NETCFG_DHCP);
    now_ms += 1000u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT32(writes, w5x_model_writes());
}

static void test_the_lease_runs_from_the_request_and_then_starts_again(void)
{
    tfw_ms_t asked;
    reply_t  r = reply(5u);

    offer_and_request();
    asked   = now_ms;
    now_ms += 500u;
    TEST_ASSERT_EQUAL(TFW_OK, acked(&r));
    now_ms = asked + T1_MS - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = asked + T1_MS;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
    now_ms = asked + LEASE_MS - 1u;                         /* never connected */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    now_ms = asked + LEASE_MS;
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));   /* INIT again */
    TEST_ASSERT_EQUAL_UINT8(0x22u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.ip[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
}

static void test_an_infinite_lease_is_held_to_the_clock_compare(void)
{
    tfw_ms_t asked;
    reply_t  r = reply(5u);

    offer_and_request();
    asked   = now_ms;
    r.lease = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL(TFW_OK, acked(&r));
    now_ms = asked + 1000000u * 1000u - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = asked + 1000000u * 1000u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
    now_ms = asked + 2000000u * 1000u - 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    now_ms = asked + 2000000u * 1000u;
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
}

static void test_discover_is_retransmitted_at_4_8_16_32_64_s_then_given_up(void)
{
    static const tfw_ms_t base[5] = { 4000u, 8000u, 16000u, 32000u, 64000u };
    tfw_ms_t last;
    uint8_t  k;

    discover();
    for (k = 0u; k < 4u; k++) {
        last = now_ms;
        TEST_ASSERT_EQUAL(TFW_ERR_BUSY, run(base[k] + 2000u));
        TEST_ASSERT_EQUAL_UINT8(1u, sent());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(xid, msg + 4, 4u);
        TEST_ASSERT_TRUE(now_ms - last >= base[k] - 1000u);
        TEST_ASSERT_TRUE(now_ms - last <= base[k] + 1000u + STEP_MS);
    }
    last = now_ms;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, run(base[4] + 2000u));
    TEST_ASSERT_TRUE(now_ms - last >= base[4] - 1000u);
    TEST_ASSERT_TRUE(now_ms - last <= base[4] + 1000u + STEP_MS);
    TEST_ASSERT_EQUAL_UINT8(5u, w5x_model_sends());
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));   /* given back */
}

static void test_a_request_without_an_answer_is_given_up_the_same_way(void)
{
    uint8_t k;

    offer_and_request();
    for (k = 0u; k < 4u; k++) {
        TEST_ASSERT_EQUAL(TFW_ERR_BUSY, run(70000u));
        TEST_ASSERT_EQUAL_UINT8(3u, sent());
    }
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, run(70000u));
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
}

/* Each is read whole and dropped: no DHCPREQUEST follows. */
static void offer_ignored(reply_t *r)
{
    uint8_t sends;

    discover();
    sends = w5x_model_sends();
    hand_up(r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8(sends, w5x_model_sends());
    /* and the next, good, offer is still taken */
    {
        reply_t good = reply(2u);

        hand_up(&good);
        TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
        TEST_ASSERT_EQUAL_UINT8(3u, sent());
    }
}

static void test_an_offer_for_another_xid_is_discarded(void)
{
    reply_t r = reply(2u);

    r.bad_xid = 0x01u;
    offer_ignored(&r);
}

static void test_an_offer_for_another_client_is_discarded(void)
{
    reply_t r = reply(2u);

    r.bad_mac = 0x01u;
    offer_ignored(&r);
}

static void test_a_request_shaped_or_cookieless_or_unterminated_reply_is_discarded(void)
{
    reply_t r = reply(2u);

    r.op = 1u;
    offer_ignored(&r);
    setUp();
    r = reply(2u);
    r.bad_cookie = 0x01u;
    offer_ignored(&r);
    setUp();
    r = reply(2u);
    r.end = 0u;
    offer_ignored(&r);
}

static void test_a_reply_from_a_port_other_than_67_is_discarded(void)
{
    reply_t r = reply(2u);

    r.sport = 68u;
    offer_ignored(&r);
}

static void test_an_overloaded_reply_is_discarded(void)
{
    reply_t r = reply(2u);

    r.overload = 1u;
    offer_ignored(&r);
}

static void test_an_offer_without_a_server_id_or_an_ack_while_selecting_is_discarded(void)
{
    reply_t r = reply(2u);

    r.server = 0;
    offer_ignored(&r);
    setUp();
    r = reply(5u);
    offer_ignored(&r);
}

static void test_a_nak_from_the_chosen_server_starts_again(void)
{
    reply_t r = reply(6u);

    offer_and_request();
    r.lease = 0u;
    r.mask  = 0u;
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));   /* INIT again */
    TEST_ASSERT_EQUAL_UINT8(0x22u, w5x_model_sock(S_SR));
}

static void test_another_servers_ack_or_nak_is_discarded(void)
{
    reply_t r = reply(6u);

    offer_and_request();
    r.server = srv_b;
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    r = reply(5u);
    r.server = srv_b;
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    r = reply(5u);
    TEST_ASSERT_EQUAL(TFW_OK, acked(&r));
}

static void test_an_ack_without_a_lease_or_a_mask_is_discarded(void)
{
    reply_t r = reply(5u);

    offer_and_request();
    r.lease = 0u;
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    r = reply(5u);
    r.mask = 0u;
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
}

static void test_an_ack_without_a_router_leaves_no_gateway(void)
{
    reply_t r = reply(5u);

    offer_and_request();
    r.router = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, acked(&r));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.gateway[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.gateway[3]);
}

static void test_a_datagram_shorter_than_its_header_gives_the_socket_back(void)
{
    reply_t r = reply(2u);

    discover();
    r.claim = 40u;
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
}

static void test_an_ack_probes_the_address_from_no_address_before_taking_it(void)
{
    reply_t r = reply(5u);
    uint8_t sends, buf[8], i;

    offer_and_request();
    sends = w5x_model_sends();
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(sends + 1u), w5x_model_sends());
    for (i = 0u; i < 4u; i++) {
        TEST_ASSERT_EQUAL_UINT8(offered[i], w5x_model_sock((uint16_t)(S_DIPR + i)));
        TEST_ASSERT_EQUAL_UINT8(0u, w5x_model_common((uint16_t)(0x0Fu + i)));  /* SIPR */
    }
    TEST_ASSERT_EQUAL_UINT16(9u, w5x_model_sock16(S_DPORT));
    TEST_ASSERT_EQUAL_UINT16(0u, w5x_model_peer_recv(buf, sizeof buf));  /* no ARP answer */
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.ip[0]);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
}

static void test_an_address_in_use_is_declined_and_asked_for_again_after_10_s(void)
{
    reply_t  r = reply(5u);
    uint8_t  buf[8], sends;
    tfw_ms_t declined;

    w5x_model_arp_silent(none);                             /* a host has it */
    offer_and_request();
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT16(1u, w5x_model_peer_recv(buf, sizeof buf));
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    declined = now_ms;
    TEST_ASSERT_EQUAL_UINT8(4u, sent_as(bcast, 0x00u, none));  /* DHCPDECLINE */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(xid, msg + 4, 4u);
    TEST_ASSERT_EQUAL_UINT8(50u, msg[243]);                 /* requested IP */
    TEST_ASSERT_EQUAL_UINT8(4u,  msg[244]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, msg + 245, 4u);
    TEST_ASSERT_EQUAL_UINT8(54u, msg[249]);                 /* server id */
    TEST_ASSERT_EQUAL_UINT8(4u,  msg[250]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(srv_a, msg + 251, 4u);
    TEST_ASSERT_EQUAL_UINT8(255u, msg[255]);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.ip[0]);
    sends = w5x_model_sends();
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, run(10000u - STEP_MS));
    TEST_ASSERT_EQUAL_UINT8(sends, w5x_model_sends());
    now_ms += STEP_MS;
    discover();
    TEST_ASSERT_TRUE(now_ms - declined >= 11000u);
}

static void test_a_probe_the_chip_will_not_send_gives_the_socket_back(void)
{
    reply_t r = reply(5u);

    offer_and_request();
    w5x_model_cr_stuck_on(0x20u);                           /* SEND */
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_dhcp_poll(&cfg));
    w5x_model_cr_stuck_on(0u);
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.ip[0]);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));   /* INIT again */
}

/* dhcp.c poll: send_msg() refused for want of room. Nothing goes on the wire,
 * the socket is given back and no address is taken. */
static void test_a_discover_the_chip_has_no_room_for_gives_the_socket_back(void)
{
    w5x_model_tx_cap(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, run(11000u));
    TEST_ASSERT_EQUAL_UINT8(0u, w5x_model_sends());
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.mask, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.gateway, 4u);
}

/* probed(): the address answered the probe, and the DHCPDECLINE does not fit.
 * The lease is not taken, and the client starts again from INIT. */
static void test_a_decline_the_chip_has_no_room_for_gives_the_socket_back(void)
{
    reply_t r = reply(5u);
    uint8_t buf[8], sends;

    w5x_model_arp_silent(none);                             /* a host has it */
    offer_and_request();
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT16(1u, w5x_model_peer_recv(buf, sizeof buf));
    sends = w5x_model_sends();
    w5x_model_tx_cap(10u);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8(sends, w5x_model_sends());
    TEST_ASSERT_EQUAL_UINT16(0u, w5x_model_peer_recv(buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.mask, 4u);
    w5x_model_tx_cap(0xFFFFu);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));   /* INIT again */
    TEST_ASSERT_EQUAL_UINT8(0x22u, w5x_model_sock(S_SR));
}

/* probed(): the controller takes the probe's SEND but never says how it went
 * (no SEND_OK, no TIMEOUT). After 4 s the client gives up without the lease. */
static void test_a_probe_the_chip_never_finishes_is_given_up_after_4_s(void)
{
    reply_t r = reply(5u);

    offer_and_request();
    hand_up(&r);
    w5x_model_set_sr(0x00u);                                /* SEND raises nothing */
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    now_ms += 4000u - 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.ip, 4u);
    now_ms += 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.mask, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.gateway, 4u);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));   /* INIT again */
    TEST_ASSERT_EQUAL_UINT8(0x22u, w5x_model_sock(S_SR));
}

static void test_at_t1_the_lease_is_renewed_beside_the_connection(void)
{
    tfw_ms_t asked = bind();
    reply_t  r     = reply(5u);

    connect_link();
    now_ms = asked + T1_MS - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = asked + T1_MS;
    renewal_sent();
    TEST_ASSERT_EQUAL_UINT8(0x22u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT16(68u, w5x_model_sock16(S_PORT));
    TEST_ASSERT_EQUAL_UINT8(0x17u, link_sr());              /* ESTABLISHED */
    TEST_ASSERT_EQUAL_UINT8(offered[3], w5x_model_common(0x12u));  /* SIPR kept */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());

    now_ms += 2000u;
    r.mask = 0u;                                            /* not needed again */
    hand_up(&r);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT8(0x17u, link_sr());
    /* The new lease counts from T1, so the next T1 is a lease after the first. */
    now_ms = asked + LEASE_MS - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = asked + LEASE_MS;
    renewal_sent();
}

static void test_a_renewal_waits_half_the_time_to_t2_but_at_least_60_s(void)
{
    tfw_ms_t asked = bind();
    tfw_ms_t t1    = asked + T1_MS;

    connect_link();
    now_ms = t1;
    renewal_sent();
    /* T2 is 1350 s on: the next request is 675 s on, then 337 s after. */
    now_ms = t1 + 675000u - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = t1 + 675000u;
    renewal_sent();
    now_ms = t1 + 675000u + 337500u;
    renewal_sent();
}

static void test_at_t2_any_server_may_rebind_the_lease(void)
{
    tfw_ms_t asked = bind();
    reply_t  r     = reply(5u);

    connect_link();
    now_ms = asked + T1_MS;
    renewal_sent();
    now_ms = asked + T2_MS;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT8(3u, sent_as(bcast, 0x00u, offered));
    TEST_ASSERT_EQUAL_UINT8(255u, msg[247]);
    memcpy(xid, msg + 4, 4u);
    r.server = srv_b;
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT8(0x17u, link_sr());
    /* From T2, and renewed from the server that rebound it. */
    now_ms = asked + T2_MS + T1_MS - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = asked + T2_MS + T1_MS;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT8(3u, sent_as(srv_b, 0x00u, offered));
}

static void test_a_short_lease_is_asked_for_60_s_apart_and_lost_at_its_end(void)
{
    tfw_ms_t asked;
    reply_t  r = reply(5u);

    offer_and_request();
    asked   = now_ms;
    r.lease = 200u;                                         /* T1 100 s, T2 175 s */
    TEST_ASSERT_EQUAL(TFW_OK, acked(&r));
    connect_link();
    now_ms = asked + 100000u;
    renewal_sent();
    now_ms = asked + 160000u - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = asked + 160000u;
    renewal_sent();
    now_ms = asked + 175000u - 1u;                          /* capped at T2 */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = asked + 175000u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT8(3u, sent_as(bcast, 0x00u, offered));
    now_ms = asked + 200000u - 1u;                          /* capped at the end */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = asked + 200000u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));   /* INIT again */
    TEST_ASSERT_EQUAL_UINT8(0u, cfg.ip[0]);
}

static void test_a_nak_to_a_renewal_loses_the_lease(void)
{
    tfw_ms_t asked = bind();
    reply_t  r     = reply(6u);

    connect_link();
    now_ms = asked + T1_MS;
    renewal_sent();
    r.server = srv_b;                                       /* not ours: ignored */
    hand_up(&r);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    r = reply(6u);
    hand_up(&r);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
}

static void test_a_connection_lost_while_renewing_renews_again_once_up(void)
{
    tfw_ms_t asked = bind();

    connect_link();
    now_ms = asked + T1_MS;
    renewal_sent();
    now_ms += 1000u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));         /* the link is down */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    connect_link();
    renewal_sent();
    TEST_ASSERT_EQUAL_UINT8(0x17u, link_sr());
}

/* tfw_dhcp_renew(): at T1 the second socket will not open. The lease still
 * runs, so nothing is sent, the connection keeps its address, and the next
 * try is due half the time to the lease's end on (900 s). */
static void test_a_renewal_socket_the_chip_will_not_open_keeps_the_lease_and_tries_later(void)
{
    tfw_ms_t asked = bind();
    tfw_ms_t t1    = asked + T1_MS;
    uint8_t  sends;

    connect_link();
    now_ms = t1;
    sends  = w5x_model_sends();
    w5x_model_cr_stuck_on(0x01u);                           /* OPEN */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    w5x_model_cr_stuck_on(0u);
    TEST_ASSERT_EQUAL_UINT8(sends, w5x_model_sends());
    TEST_ASSERT_EQUAL_UINT8(0x00u, w5x_model_sock(S_SR));
    TEST_ASSERT_EQUAL_UINT8(0x17u, link_sr());
    TEST_ASSERT_EQUAL_UINT8(offered[3], w5x_model_common(0x12u));  /* SIPR kept */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = t1 + 900000u - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    now_ms = t1 + 900000u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_dhcp_due());
}

/* tfw_dhcp_renew(): the renewal does not fit the socket. The answer is still
 * OK -- the lease runs -- nothing is sent, and the next request goes at its
 * usual time, half the way to T2 (675 s). */
static void test_a_renewal_the_chip_has_no_room_for_keeps_the_lease_and_goes_next_time(void)
{
    tfw_ms_t asked = bind();
    tfw_ms_t t1    = asked + T1_MS;
    uint8_t  sends;

    connect_link();
    now_ms = t1;
    sends  = w5x_model_sends();
    w5x_model_tx_cap(0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    w5x_model_tx_cap(0xFFFFu);
    TEST_ASSERT_EQUAL_UINT8(sends, w5x_model_sends());
    TEST_ASSERT_EQUAL_UINT8(0x17u, link_sr());
    TEST_ASSERT_EQUAL_UINT8(offered[3], w5x_model_common(0x12u));  /* SIPR kept */
    now_ms = t1 + 675000u - 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    now_ms = t1 + 675000u;
    renewal_sent();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_static_block_is_used_as_it_is_and_the_chip_untouched);
    RUN_TEST(test_the_socket_opens_as_udp_on_port_68_with_no_address);
    RUN_TEST(test_the_first_discover_waits_between_1_and_10_s);
    RUN_TEST(test_the_other_chip_is_refused_and_no_address_is_taken);
    RUN_TEST(test_an_offer_is_answered_with_a_request_naming_its_server_and_address);
    RUN_TEST(test_an_ack_hands_the_link_the_lease_and_gives_the_socket_back);
    RUN_TEST(test_a_bound_client_answers_from_its_lease_without_the_chip);
    RUN_TEST(test_the_lease_runs_from_the_request_and_then_starts_again);
    RUN_TEST(test_an_infinite_lease_is_held_to_the_clock_compare);
    RUN_TEST(test_discover_is_retransmitted_at_4_8_16_32_64_s_then_given_up);
    RUN_TEST(test_a_request_without_an_answer_is_given_up_the_same_way);
    RUN_TEST(test_an_offer_for_another_xid_is_discarded);
    RUN_TEST(test_an_offer_for_another_client_is_discarded);
    RUN_TEST(test_a_request_shaped_or_cookieless_or_unterminated_reply_is_discarded);
    RUN_TEST(test_a_reply_from_a_port_other_than_67_is_discarded);
    RUN_TEST(test_an_overloaded_reply_is_discarded);
    RUN_TEST(test_an_offer_without_a_server_id_or_an_ack_while_selecting_is_discarded);
    RUN_TEST(test_a_nak_from_the_chosen_server_starts_again);
    RUN_TEST(test_another_servers_ack_or_nak_is_discarded);
    RUN_TEST(test_an_ack_without_a_lease_or_a_mask_is_discarded);
    RUN_TEST(test_an_ack_without_a_router_leaves_no_gateway);
    RUN_TEST(test_a_datagram_shorter_than_its_header_gives_the_socket_back);
    RUN_TEST(test_an_ack_probes_the_address_from_no_address_before_taking_it);
    RUN_TEST(test_an_address_in_use_is_declined_and_asked_for_again_after_10_s);
    RUN_TEST(test_a_probe_the_chip_will_not_send_gives_the_socket_back);
    RUN_TEST(test_a_discover_the_chip_has_no_room_for_gives_the_socket_back);
    RUN_TEST(test_a_decline_the_chip_has_no_room_for_gives_the_socket_back);
    RUN_TEST(test_a_probe_the_chip_never_finishes_is_given_up_after_4_s);
    RUN_TEST(test_at_t1_the_lease_is_renewed_beside_the_connection);
    RUN_TEST(test_a_renewal_waits_half_the_time_to_t2_but_at_least_60_s);
    RUN_TEST(test_at_t2_any_server_may_rebind_the_lease);
    RUN_TEST(test_a_short_lease_is_asked_for_60_s_apart_and_lost_at_its_end);
    RUN_TEST(test_a_nak_to_a_renewal_loses_the_lease);
    RUN_TEST(test_a_connection_lost_while_renewing_renews_again_once_up);
    RUN_TEST(test_a_renewal_socket_the_chip_will_not_open_keeps_the_lease_and_tries_later);
    RUN_TEST(test_a_renewal_the_chip_has_no_room_for_keeps_the_lease_and_goes_next_time);
    return UNITY_END();
}
