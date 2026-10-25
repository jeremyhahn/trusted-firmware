/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Ethernet image's link -- issue #55 -- end to end through the real
 * transport, dispatch and network block, with the UART and EEPROM from the
 * mock HAL and the TCP socket faked here at include/tfw/eth.h. The drivers
 * under that seam have their own suites (test_w5100, test_w5500); this one is
 * about what the link decides: which wire a frame belongs to, what the network
 * may ask for, where events go, and when it reconnects.
 *
 * link_eth.c keeps its state across tests, as it does across a device's life.
 * setUp() therefore drops whatever connection the last test left, then moves
 * the clock past the longest backoff, so every test starts DOWN and due.
 */

#include <string.h>
#include "unity.h"
#include "transport.h"
#include "sys.h"
#include "crc16.h"
#include "hal_mock.h"
#include "tfw/eth.h"
#include "tfw/link.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"

/* --------------------------------------------------------------- the socket */

static struct {
    uint8_t      state;
    uint8_t      absent;          /* tfw_eth_start() finds no controller */
    unsigned     starts, closes, sends;
    tfw_netcfg_t cfg;
    uint16_t     port;
    uint8_t      in[256];
    uint16_t     in_len, in_pos;
    uint8_t      q[256];          /* tfw_eth_tx() so far, not yet sent */
    uint16_t     q_len, q_cap;
    uint8_t      out[512];        /* what tfw_eth_send() put on the wire */
    uint16_t     out_len;
} eth;

tfw_status_t tfw_eth_start(const tfw_netcfg_t *cfg, uint16_t local_port)
{
    eth.starts++;
    eth.cfg  = *cfg;
    eth.port = local_port;
    if (eth.absent) {
        return TFW_ERR_UNSUPPORTED;
    }
    eth.state = TFW_ETH_BUSY;
    return TFW_OK;
}

uint8_t tfw_eth_state(void)
{
    return eth.state;
}

uint8_t tfw_eth_rx(uint8_t *out)
{
    if (eth.in_pos == eth.in_len) {
        return 0u;
    }
    *out = eth.in[eth.in_pos++];
    return 1u;
}

uint8_t tfw_eth_tx(uint8_t byte)
{
    if (eth.q_len == eth.q_cap) {
        return 0u;
    }
    eth.q[eth.q_len++] = byte;
    return 1u;
}

void tfw_eth_send(void)
{
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

static void eth_feed(const uint8_t *b, uint16_t n)
{
    memcpy(&eth.in[eth.in_len], b, n);
    eth.in_len = (uint16_t)(eth.in_len + n);
}

/* -------------------------------------------------------------- the fixture */

/* A provisioned block, literal, CRC included: MAC 02:00:00:00:00:01, device
 * 192.0.2.10/24 via 192.0.2.1, endpoint 192.0.2.20:7020. sdk/go's NetConfig
 * test pins the same 32 bytes, so the host that writes the block and the
 * device that reads it agree on every byte. */
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

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_transport_init(&t);
    tfw_framer_init(&rxf);

    clock_ms += 1000000u;
    tfw_mock_set_millis(clock_ms);
    memset(&eth, 0, sizeof eth);
    eth.q_cap = (uint16_t)sizeof eth.q;
    tfw_link_poll();                       /* drop what the last test left */
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    eth.starts = eth.closes = 0u;
}
void tearDown(void) {}

static void provision(void)
{
    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], NETCFG, sizeof NETCFG);
}

/* Provisioned, attempted, established. Leaves the link's backoff at zero. */
static void connect_up(void)
{
    provision();
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

static uint16_t request(uint8_t *wire, uint8_t seq, uint8_t cmd, const uint8_t *pay, uint8_t n)
{
    uint16_t len = tfw_frame_encode_request(wire, TFW_MAX_FRAME, seq, cmd, pay, n);

    TEST_ASSERT_NOT_EQUAL(0u, len);
    return len;
}

static void poll_n(unsigned n)
{
    while (n--) {
        tfw_transport_poll(&t);
    }
}

/* One request over Ethernet. Returns 1 if a reply came back over Ethernet, and
 * asserts nothing went out on serial. */
static uint8_t over_eth(uint8_t seq, uint8_t cmd, const uint8_t *pay, uint8_t n)
{
    uint8_t wire[TFW_MAX_FRAME];

    eth_feed(wire, request(wire, seq, cmd, pay, n));
    eth.out_len = 0u;
    poll_n(8u);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_tx_pending());
    return decode(eth.out, eth.out_len);
}

/* One request over serial. Returns 1 if a reply came back over serial, and
 * asserts nothing went out on Ethernet. */
static uint8_t over_serial(uint8_t seq, uint8_t cmd, const uint8_t *pay, uint8_t n)
{
    uint8_t wire[TFW_MAX_FRAME];
    uint8_t back[TFW_MAX_FRAME * 2u];
    size_t  got;

    tfw_mock_feed_rx(wire, request(wire, seq, cmd, pay, n));
    eth.out_len = 0u;
    poll_n(8u);
    got = tfw_mock_drain_tx(back, sizeof back);
    TEST_ASSERT_EQUAL_UINT16(0u, eth.out_len);
    return decode(back, got);
}

/* ------------------------------------------------------------ network block */

static void test_the_literal_block_decodes_to_its_fields(void)
{
    static const uint8_t mac[6] = { 0x02, 0, 0, 0, 0, 0x01 };
    static const uint8_t ip[4] = { 192, 0, 2, 10 }, mask[4] = { 255, 255, 255, 0 };
    static const uint8_t gw[4] = { 192, 0, 2, 1 },  peer[4] = { 192, 0, 2, 20 };
    tfw_netcfg_t c;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_netcfg_decode(NETCFG, &c));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, c.mac, 6u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ip, c.ip, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mask, c.mask, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(gw, c.gateway, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(peer, c.peer, 4u);
    TEST_ASSERT_EQUAL_UINT16(7020u, c.port);
    TEST_ASSERT_EQUAL_HEX8(0x00u, c.flags);                /* static addresses */
}

static void test_the_block_lives_at_0x140_for_32_bytes(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0140u, TFW_NETCFG_ADDR);   /* the reserved end, #125 */
    TEST_ASSERT_EQUAL_UINT(32u, TFW_NETCFG_LEN);
}

/* Each field that makes a block unusable, one at a time. The last two carry a
 * good CRC, so it is the zero that refuses them and not the checksum. */
static void test_a_damaged_or_incomplete_block_is_not_provisioned(void)
{
    uint8_t      b[TFW_NETCFG_LEN];
    tfw_netcfg_t c;
    uint16_t     crc;

    memcpy(b, NETCFG, sizeof b); b[0] = 'X';
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_decode(b, &c));
    memcpy(b, NETCFG, sizeof b); b[4] = 2u;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_decode(b, &c));
    memcpy(b, NETCFG, sizeof b); b[31] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_decode(b, &c));

    memcpy(b, NETCFG, sizeof b); b[28] = b[29] = 0u;
    crc = tfw_crc16(b, 30u); b[30] = (uint8_t)crc; b[31] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_decode(b, &c));

    memcpy(b, NETCFG, sizeof b); memset(&b[24], 0, 4u);
    crc = tfw_crc16(b, 30u); b[30] = (uint8_t)crc; b[31] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_decode(b, &c));
}

/* -------------------------------------------------------------- connecting */

static void test_a_blank_block_never_starts_the_controller_and_serial_still_answers(void)
{
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0u, eth.starts);
    TEST_ASSERT_TRUE(over_serial(5u, TFW_CMD_PING, 0, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
}

/* Flags bit 0 asks for a DHCP lease. This link has src/net/dhcp_none.c, as
 * every image built without DHCP=1 does: the block decodes, but the link never
 * connects from addresses nobody gave it, and serial is untouched. */
static void test_a_block_asking_for_dhcp_stays_down_without_the_client(void)
{
    uint8_t      b[TFW_NETCFG_LEN];
    tfw_netcfg_t c;
    uint16_t     crc;

    memcpy(b, NETCFG, sizeof b); b[5] = TFW_NETCFG_DHCP;
    crc = tfw_crc16(b, 30u); b[30] = (uint8_t)crc; b[31] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_netcfg_decode(b, &c));
    TEST_ASSERT_EQUAL_HEX8(TFW_NETCFG_DHCP, c.flags);

    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], b, sizeof b);
    tfw_link_poll();
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0u, eth.starts);
    TEST_ASSERT_TRUE(over_serial(6u, TFW_CMD_PING, 0, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
}

/* Without the client no lease ever falls due, and a renewal of the block's own
 * addresses succeeds: link_eth.c drops a live connection on a failed renewal,
 * so anything else here would take a static link down. */
static void test_without_the_client_nothing_falls_due_and_renewal_keeps_the_link(void)
{
    tfw_netcfg_t c;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_netcfg_decode(NETCFG, &c));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_dhcp_due());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&c));
}

static void test_the_block_from_eeprom_is_what_the_controller_is_given(void)
{
    tfw_netcfg_t want;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_netcfg_decode(NETCFG, &want));
    provision();
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want.mac, eth.cfg.mac, 6u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want.ip, eth.cfg.ip, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want.mask, eth.cfg.mask, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want.gateway, eth.cfg.gateway, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want.peer, eth.cfg.peer, 4u);
    TEST_ASSERT_EQUAL_UINT16(7020u, eth.cfg.port);
    TEST_ASSERT_TRUE(eth.port >= 49152u);
}

/* Address, MAC and endpoint are configuration, never a literal in src/: a
 * second block, different from NETCFG in every field, reaches the controller
 * whole, and the next block written replaces it whole. */
static void test_every_field_the_controller_is_given_comes_from_the_block(void)
{
    static const uint8_t mac[6] = { 0x06u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u };
    static const uint8_t ip[4] = { 198u, 51u, 100u, 7u }, mask[4] = { 255u, 255u, 0u, 0u };
    static const uint8_t gw[4] = { 198u, 51u, 100u, 254u }, peer[4] = { 203u, 0u, 113u, 9u };
    uint8_t  b[TFW_NETCFG_LEN];
    uint16_t crc;

    memcpy(b, NETCFG, sizeof b);
    memcpy(&b[6], mac, 6u);
    memcpy(&b[12], ip, 4u);
    memcpy(&b[16], mask, 4u);
    memcpy(&b[20], gw, 4u);
    memcpy(&b[24], peer, 4u);
    b[28] = 0x9Cu; b[29] = 0x41u;                                  /* port 40001 */
    crc = tfw_crc16(b, 30u); b[30] = (uint8_t)crc; b[31] = (uint8_t)(crc >> 8);
    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], b, sizeof b);

    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, eth.cfg.mac, 6u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ip, eth.cfg.ip, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mask, eth.cfg.mask, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(gw, eth.cfg.gateway, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(peer, eth.cfg.peer, 4u);
    TEST_ASSERT_EQUAL_UINT16(40001u, eth.cfg.port);

    eth.state = TFW_ETH_DOWN;                                      /* the attempt fails */
    tfw_link_poll();
    provision();
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(2u, eth.starts);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&NETCFG[6], eth.cfg.mac, 6u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&NETCFG[12], eth.cfg.ip, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&NETCFG[16], eth.cfg.mask, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&NETCFG[20], eth.cfg.gateway, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&NETCFG[24], eth.cfg.peer, 4u);
    TEST_ASSERT_EQUAL_UINT16(7020u, eth.cfg.port);
}

/* A provisioned block behind an EEPROM that refuses the read is no block:
 * nothing is decoded and the controller is never started. */
static void test_a_refused_eeprom_read_reads_as_unprovisioned(void)
{
    tfw_netcfg_t got;

    provision();
    tfw_mock_eeprom_read_fails(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_load(&got));
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(0u, eth.starts);
}

/* The block is written over serial, then picked up by the next attempt with
 * no restart: provisioning is EEPROM_WRITE and nothing else. */
static void test_a_block_written_over_serial_is_used_at_the_next_attempt(void)
{
    uint8_t  p[2u + 8u];
    uint16_t off;

    for (off = 0u; off < TFW_NETCFG_LEN; off = (uint16_t)(off + 8u)) {   /* EEPROM_WRITE's chunk */
        p[0] = (uint8_t)(TFW_NETCFG_ADDR + off);
        p[1] = (uint8_t)((TFW_NETCFG_ADDR + off) >> 8);
        memcpy(&p[2], &NETCFG[off], 8u);
        TEST_ASSERT_TRUE(over_serial((uint8_t)(1u + off), TFW_CMD_EEPROM_WRITE, p, (uint8_t)sizeof p));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
    }

    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
    TEST_ASSERT_EQUAL_UINT16(7020u, eth.cfg.port);
}

/* 1 s after a failure, doubling to 64 s and holding there; a connection that
 * comes up puts it back to 1 s. Nothing here waits: each step is a poll at a
 * time, and a poll one millisecond early does nothing. */
static void test_reconnect_backs_off_from_1_s_to_64_s_and_resets_once_up(void)
{
    static const tfw_ms_t schedule[] = { 1000u, 2000u, 4000u, 8000u, 16000u,
                                         32000u, 64000u, 64000u };
    uint16_t last_port;
    unsigned i;

    connect_up();
    eth.absent = 1u;
    eth.state  = TFW_ETH_DOWN;            /* the endpoint went away */
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.closes);

    for (i = 0; i < sizeof schedule / sizeof schedule[0]; i++) {
        last_port = eth.port;
        eth.starts = 0u;
        tfw_mock_advance(schedule[i] - 1u);
        tfw_link_poll();
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, eth.starts, "attempted early");
        tfw_mock_advance(1u);
        tfw_link_poll();
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, eth.starts, "not attempted on time");
        TEST_ASSERT_NOT_EQUAL(last_port, eth.port);  /* a fresh source port */
    }

    /* Back up: the next failure waits 1 s again, not 64. */
    eth.absent = 0u;
    tfw_mock_advance(64000u);
    tfw_link_poll();
    eth.state = TFW_ETH_UP;
    tfw_link_poll();
    eth.state = TFW_ETH_DOWN;
    tfw_link_poll();
    eth.starts = 0u;
    tfw_mock_advance(1000u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
}

static void test_a_refused_connect_is_closed_and_retried(void)
{
    provision();
    tfw_link_poll();
    eth.state = TFW_ETH_DOWN;             /* RST, or the connect timed out */
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.closes);
    tfw_mock_advance(LONGEST_BACKOFF_MS);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(2u, eth.starts);
}

/* --------------------------------------------------------------- binding */

static void test_a_frame_over_ethernet_is_answered_over_ethernet(void)
{
    connect_up();
    TEST_ASSERT_TRUE(over_eth(0x31u, TFW_CMD_CAPS, 0, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x31u, tfw_frame_seq(&rxf));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT(1u, eth.sends);   /* one reply, one SEND */
}

static void test_a_frame_over_serial_is_answered_over_serial_while_ethernet_is_up(void)
{
    connect_up();
    TEST_ASSERT_TRUE(over_serial(0x32u, TFW_CMD_PING, 0, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT(0u, eth.sends);
}

/* A frame half-arrived on Ethernet holds the transport to that wire: a whole
 * serial frame landing meanwhile waits in the UART ring, is not mixed into the
 * Ethernet frame, and is answered next, on serial. */
static void test_a_frame_in_progress_holds_its_wire_and_the_other_waits(void)
{
    uint8_t ew[TFW_MAX_FRAME], sw[TFW_MAX_FRAME], back[TFW_MAX_FRAME * 2u];
    uint16_t en, sn;
    size_t  got;

    connect_up();
    en = request(ew, 0x41u, TFW_CMD_CAPS, 0, 0u);
    sn = request(sw, 0x42u, TFW_CMD_PING, 0, 0u);

    eth_feed(ew, 3u);
    tfw_transport_poll(&t);
    TEST_ASSERT_TRUE(t.in_frame);
    tfw_mock_feed_rx(sw, sn);
    eth_feed(&ew[3], (uint16_t)(en - 3u));
    poll_n(16u);

    TEST_ASSERT_TRUE(decode(eth.out, eth.out_len));
    TEST_ASSERT_EQUAL_HEX8(0x41u, tfw_frame_seq(&rxf));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
    got = tfw_mock_drain_tx(back, sizeof back);
    TEST_ASSERT_TRUE(decode(back, got));
    TEST_ASSERT_EQUAL_HEX8(0x42u, tfw_frame_seq(&rxf));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
}

/* Neither wire starves the other: with a frame waiting on each, both are
 * answered, each on its own wire. */
static void test_frames_waiting_on_both_wires_are_both_answered(void)
{
    uint8_t ew[TFW_MAX_FRAME], sw[TFW_MAX_FRAME], back[TFW_MAX_FRAME * 2u];
    size_t  got;

    connect_up();
    eth_feed(ew, request(ew, 0x51u, TFW_CMD_CAPS, 0, 0u));
    tfw_mock_feed_rx(sw, request(sw, 0x52u, TFW_CMD_PING, 0, 0u));
    poll_n(16u);

    TEST_ASSERT_TRUE(decode(eth.out, eth.out_len));
    TEST_ASSERT_EQUAL_HEX8(0x51u, tfw_frame_seq(&rxf));
    got = tfw_mock_drain_tx(back, sizeof back);
    TEST_ASSERT_TRUE(decode(back, got));
    TEST_ASSERT_EQUAL_HEX8(0x52u, tfw_frame_seq(&rxf));
}

/* A reply owed to a connection that drops mid-send is abandoned, and serial
 * is answered straight after: a dead peer does not hold the transport. */
static void test_a_reply_to_a_dropped_connection_is_abandoned_and_serial_carries_on(void)
{
    uint8_t ew[TFW_MAX_FRAME];

    connect_up();
    eth.q_cap = 4u;                       /* the reply cannot finish */
    eth_feed(ew, request(ew, 0x61u, TFW_CMD_CAPS, 0, 0u));
    poll_n(4u);
    TEST_ASSERT_EQUAL_UINT(0u, eth.sends);

    eth.state = TFW_ETH_DOWN;
    tfw_transport_poll(&t);
    TEST_ASSERT_EQUAL_UINT(1u, eth.closes);
    TEST_ASSERT_EQUAL_UINT(0u, eth.sends);

    TEST_ASSERT_TRUE(over_serial(0x62u, TFW_CMD_PING, 0, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x62u, tfw_frame_seq(&rxf));
}

/* Events are for the bench host on serial, even when the last frame came in
 * over Ethernet. */
static void test_events_go_to_serial_only(void)
{
    uint8_t back[TFW_MAX_FRAME];
    size_t  got;

    connect_up();
    TEST_ASSERT_TRUE(over_eth(0x71u, TFW_CMD_CAPS, 0, 0u));
    eth.out_len = 0u;
    eth.sends = 0u;

    TEST_ASSERT_EQUAL_UINT8(1u, tfw_transport_send_event(&t, TFW_EVENT_BOOT));
    poll_n(2u);
    TEST_ASSERT_EQUAL_UINT(0u, eth.sends);
    TEST_ASSERT_EQUAL_UINT16(0u, eth.out_len);
    got = tfw_mock_drain_tx(back, sizeof back);
    TEST_ASSERT_TRUE(decode(back, got));
    TEST_ASSERT_EQUAL_HEX8(TFW_OP_EVENT, tfw_frame_op(&rxf));
    TEST_ASSERT_EQUAL_HEX8(TFW_EVENT_BOOT, tfw_frame_payload(&rxf)[0]);
}

/* ------------------------------------------------------------- allow-list */

/* THE ALLOW-LIST, as literals. Every id a request can carry, against the link
 * with the current frame on Ethernet. A change here is a decision about what
 * the network may ask the device for, made in the ADR first. */
static void test_ethernet_admits_the_enrolment_attestation_and_staging_commands_and_nothing_else(void)
{
    /* 0x68..0x6B: OTA staging, ADR decision 8 and owner answer 8 (issue #75),
     * and no further update command: owner ruling, issue #55 item 48. */
    static const uint8_t admitted[] = { 0x00u, 0x06u, 0x60u, 0x63u, 0x64u,
                                        0x68u, 0x69u, 0x6Au, 0x6Bu };
    unsigned id, i;
    uint8_t  want;

    connect_up();
    TEST_ASSERT_TRUE(over_eth(1u, TFW_CMD_CAPS, 0, 0u));   /* the frame is Ethernet's */
    for (id = 0u; id <= 0x7Fu; id++) {
        want = 0u;
        for (i = 0u; i < sizeof admitted; i++) {
            want = (uint8_t)(want | (admitted[i] == id));
        }
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want, tfw_link_admits((uint8_t)id), "allow-list moved");
    }
}

static void test_serial_admits_every_command(void)
{
    unsigned id;

    connect_up();
    TEST_ASSERT_TRUE(over_serial(1u, TFW_CMD_PING, 0, 0u));
    for (id = 0u; id <= 0x7Fu; id++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_admits((uint8_t)id));
    }
}

/* A refused command reads as one the image does not have -- the status an
 * older host already knows -- with no payload, and nothing has run: the
 * network cannot rewrite the block that tells the device where to connect. */
static void test_a_command_off_the_list_is_answered_unknown_and_does_not_run(void)
{
    uint8_t p[2u + 4u] = {   /* a well-formed write, so only the list refuses it */ (uint8_t)TFW_NETCFG_ADDR, (uint8_t)(TFW_NETCFG_ADDR >> 8),
                           0xDEu, 0xADu, 0xBEu, 0xEFu };

    connect_up();
    TEST_ASSERT_TRUE(over_eth(0x81u, TFW_CMD_EEPROM_WRITE, p, (uint8_t)sizeof p));
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&rxf));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NETCFG, &tfw_mock_eeprom()[TFW_NETCFG_ADDR], TFW_NETCFG_LEN);

    TEST_ASSERT_TRUE(over_eth(0x82u, TFW_CMD_PING, 0, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_frame_status(&rxf));
}

/* The update commands the network may send are 0x68..0x6B and no more (owner
 * ruling, issue #55 item 48): the OTA ADR's widening to the auth pair and to
 * OTA_INFO..OTA_CONFIRM is narrowed away, so AUTH_CHALLENGE and the first id
 * past the range read as commands the image does not have. */
static void test_an_update_command_past_0x6b_is_answered_unknown_over_ethernet(void)
{
    connect_up();
    TEST_ASSERT_TRUE(over_eth(0x83u, TFW_CMD_AUTH_CHALLENGE, 0, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&rxf));

    TEST_ASSERT_TRUE(over_eth(0x84u, 0x6Cu, 0, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&rxf));
}

/* Every id off the allow-list, not a sample: each is answered over Ethernet
 * with TFW_ERR_UNKNOWN_CMD and no payload, and the connection stays up. */
static void test_every_id_off_the_list_is_answered_unknown_over_ethernet(void)
{
    static const uint8_t admitted[] = { 0x00u, 0x06u, 0x60u, 0x63u, 0x64u,
                                        0x68u, 0x69u, 0x6Au, 0x6Bu };
    unsigned id, i, on, refused = 0u;

    connect_up();
    for (id = 0u; id <= 0x7Fu; id++) {
        on = 0u;
        for (i = 0u; i < sizeof admitted; i++) {
            on |= (unsigned)(admitted[i] == id);
        }
        if (on) {
            continue;
        }
        TEST_ASSERT_EQUAL_UINT16(eth.in_len, eth.in_pos);      /* the last frame was read */
        eth.in_len = 0u;                                       /* reuse the mock's buffer */
        eth.in_pos = 0u;
        TEST_ASSERT_TRUE_MESSAGE(over_eth((uint8_t)(id + 1u), (uint8_t)id, 0, 0u), "no answer");
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_UNKNOWN_CMD, tfw_frame_status(&rxf), "refused id answered otherwise");
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&rxf));
        refused++;
    }
    TEST_ASSERT_EQUAL_UINT(0x80u - sizeof admitted, refused);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NETCFG, &tfw_mock_eeprom()[TFW_NETCFG_ADDR], TFW_NETCFG_LEN);
}

/* ATTEST over Ethernet reaches the attestation module: whatever it answers,
 * it is not "unknown command". */
static void test_attest_over_ethernet_reaches_the_attestation_module(void)
{
    uint8_t nonce[32];

    memset(nonce, 0xA5u, sizeof nonce);
    connect_up();
    TEST_ASSERT_TRUE(over_eth(0x91u, TFW_CMD_ATTEST, nonce, (uint8_t)sizeof nonce));
    TEST_ASSERT_NOT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_frame_status(&rxf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_literal_block_decodes_to_its_fields);
    RUN_TEST(test_the_block_lives_at_0x140_for_32_bytes);
    RUN_TEST(test_a_damaged_or_incomplete_block_is_not_provisioned);
    RUN_TEST(test_a_blank_block_never_starts_the_controller_and_serial_still_answers);
    RUN_TEST(test_a_block_asking_for_dhcp_stays_down_without_the_client);
    RUN_TEST(test_without_the_client_nothing_falls_due_and_renewal_keeps_the_link);
    RUN_TEST(test_the_block_from_eeprom_is_what_the_controller_is_given);
    RUN_TEST(test_every_field_the_controller_is_given_comes_from_the_block);
    RUN_TEST(test_a_refused_eeprom_read_reads_as_unprovisioned);
    RUN_TEST(test_a_block_written_over_serial_is_used_at_the_next_attempt);
    RUN_TEST(test_reconnect_backs_off_from_1_s_to_64_s_and_resets_once_up);
    RUN_TEST(test_a_refused_connect_is_closed_and_retried);
    RUN_TEST(test_a_frame_over_ethernet_is_answered_over_ethernet);
    RUN_TEST(test_a_frame_over_serial_is_answered_over_serial_while_ethernet_is_up);
    RUN_TEST(test_a_frame_in_progress_holds_its_wire_and_the_other_waits);
    RUN_TEST(test_frames_waiting_on_both_wires_are_both_answered);
    RUN_TEST(test_a_reply_to_a_dropped_connection_is_abandoned_and_serial_carries_on);
    RUN_TEST(test_events_go_to_serial_only);
    RUN_TEST(test_ethernet_admits_the_enrolment_attestation_and_staging_commands_and_nothing_else);
    RUN_TEST(test_serial_admits_every_command);
    RUN_TEST(test_a_command_off_the_list_is_answered_unknown_and_does_not_run);
    RUN_TEST(test_an_update_command_past_0x6b_is_answered_unknown_over_ethernet);
    RUN_TEST(test_every_id_off_the_list_is_answered_unknown_over_ethernet);
    RUN_TEST(test_attest_over_ethernet_reaches_the_attestation_module);
    return UNITY_END();
}
