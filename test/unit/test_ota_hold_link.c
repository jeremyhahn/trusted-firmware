/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OTA_AUTH's hold through the real transport, with the Ethernet link (#137).
 * The authorization is held for the next command only: a request the device
 * refuses without dispatching it -- one the Ethernet link does not admit, a
 * frame whose CRC failed -- is a command in between, and drops it.
 *
 * Linked by mk/host.mk as test_link_eth's link with test_ota_single's stager
 * and OTA_ACTIVATE. OTA_AUTH and OTA_ACTIVATE are off the Ethernet list, so
 * they go over serial; the refusal in between comes over either wire. The
 * image is staged by calling the stager's handlers directly, which leaves the
 * dispatcher's last command alone.
 */

#include <string.h>
#include "unity.h"
#include "transport.h"
#include "sys.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "ota_store.h"
#include "boot_platform.h"
#include "sha256.h"
#include "tfw/eth.h"
#include "tfw/link.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "tfw/protocol.h"

#define TRAILER   128u
#define CHUNK     60u      /* TFW_MAX_PAYLOAD less the offset */

/* --------------------------------------------------------------- the socket */

static struct {
    uint8_t      state;
    unsigned     starts;
    uint8_t      in[256];
    uint16_t     in_len, in_pos;
    uint8_t      q[256];
    uint16_t     q_len;
    uint8_t      out[512];
    uint16_t     out_len;
} eth;

tfw_status_t tfw_eth_start(const tfw_netcfg_t *cfg, uint16_t local_port)
{
    (void)cfg;
    (void)local_port;
    eth.starts++;
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
    if (eth.q_len == (uint16_t)sizeof eth.q) {
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
}

void tfw_eth_close(void)
{
    eth.state = TFW_ETH_DOWN;
}

/* -------------------------------------------------------------- the fixture */

/* test_link_eth's provisioned block, literal, CRC included. */
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
static uint8_t         img[TFW_OTA_STORE_STAGE_LEN];

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_nor_model_reset();
    tfw_stage_init();   /* each case a boot: the OTA_BEGIN budget is per boot (#232) */
    tfw_transport_init(&t);
    tfw_framer_init(&rxf);

    clock_ms += 1000000u;
    tfw_mock_set_millis(clock_ms);
    memset(&eth, 0, sizeof eth);
    tfw_link_poll();                       /* drop what the last test left */
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    eth.starts = 0u;
}
void tearDown(void) {}

static void connect_up(void)
{
    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], NETCFG, sizeof NETCFG);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(1u, eth.starts);
    eth.state = TFW_ETH_UP;
    tfw_link_poll();
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* test_ota_single's image: 200 bytes and a version-1 trailer in 512. */
static void build(uint32_t total, uint32_t image_len)
{
    tfw_sha256_t c;
    uint8_t     *tr = &img[total - TRAILER];
    uint8_t      meas[32];
    uint32_t     i;

    memset(img, 0xFF, sizeof img);
    for (i = 0u; i < image_len; i++) { img[i] = (uint8_t)(i * 7u + 3u); }
    tfw_sha256_init(&c);
    tfw_sha256_update(&c, img, (uint16_t)image_len);
    tfw_sha256_final(&c, meas);
    memset(tr, 0, TRAILER);
    tr[0] = 0x54u; tr[1] = 0x46u; tr[2] = 0x49u; tr[3] = 0x4Du;   /* "TFIM" */
    tr[4] = 0x01u; tr[5] = 0x00u;                                 /* version 1 */
    tr[6] = 0x01u;                                                /* the 328P */
    put32(&tr[8], 3u);
    put32(&tr[12], image_len);
    memcpy(&tr[16], meas, 32u);
}

static tfw_status_t call(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                         const uint8_t *in, uint8_t n)
{
    uint8_t      out[44];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, sizeof out);
    return fn(&r, &w);
}

static void stage(void)
{
    uint8_t             in[4u + CHUNK];
    uint8_t             raw[TFW_BOOT_EE_BLOCK_LEN];
    tfw_boot_platform_t p;
    uint32_t            off;
    uint8_t             n;

    memset(&p, 0, sizeof p);
    p.rollback_floor = 1u;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_BLOCK_LEN));

    build(512u, 200u);
    put32(in, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_begin, in, 4u));
    for (off = 0u; off < 512u; off += CHUNK) {
        n = (uint8_t)(512u - off < CHUNK ? 512u - off : CHUNK);
        put32(in, off);
        memcpy(&in[4], &img[off], n);
        TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_data, in, (uint8_t)(4u + n)));
    }
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_end, NULL, 0u));
}

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

/* ---------------------------------------------------------------- the wires */

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

/* The status of the one reply in b[0..n). */
static tfw_status_t reply(const uint8_t *b, size_t n)
{
    size_t i;

    tfw_framer_init(&rxf);
    for (i = 0; i < n; i++) {
        if (tfw_framer_push(&rxf, b[i]) == TFW_FRAME_READY) {
            return (tfw_status_t)tfw_frame_status(&rxf);
        }
    }
    TEST_FAIL_MESSAGE("no reply");
    return TFW_OK;
}

/* Sends the frame over serial, flipping its last (CRC) byte if wreck, and
 * returns the status the device answered. */
static tfw_status_t serial(uint8_t seq, uint8_t cmd, const uint8_t *pay, uint8_t n, uint8_t wreck)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint8_t  back[TFW_MAX_FRAME * 2u];
    uint16_t len = request(wire, seq, cmd, pay, n);

    if (wreck) {
        wire[len - 1u] ^= 0xFFu;
    }
    tfw_mock_feed_rx(wire, len);
    poll_n(8u);
    return reply(back, tfw_mock_drain_tx(back, sizeof back));
}

static tfw_status_t over_eth(uint8_t seq, uint8_t cmd, const uint8_t *pay, uint8_t n, uint8_t wreck)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = request(wire, seq, cmd, pay, n);

    if (wreck) {
        wire[len - 1u] ^= 0xFFu;
    }
    memcpy(&eth.in[eth.in_len], wire, len);
    eth.in_len = (uint16_t)(eth.in_len + len);
    eth.out_len = 0u;
    poll_n(8u);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_tx_pending());
    return reply(eth.out, eth.out_len);
}

static void authorize(uint8_t seq)
{
    uint8_t sig[TFW_OTA_AUTH_SIG_LEN];

    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_OK, serial(seq, TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig, 0u));
}

/* The peer goes and comes back: the link sees the socket down, closes it,
 * backs off and dials a new connection. */
static void reconnect(void)
{
    eth.state = TFW_ETH_DOWN;
    tfw_link_poll();
    tfw_mock_advance(LONGEST_BACKOFF_MS + 1u);
    tfw_link_poll();
    TEST_ASSERT_EQUAL_UINT(2u, eth.starts);
    eth.state = TFW_ETH_UP;
    tfw_link_poll();
}

static const uint8_t M0 = 0u;      /* OTA_ACTIVATE's mode byte */

/* ------------------------------------------------------------------- tests */

static void test_ota_auth_then_activate_through_the_transport_marks_the_image(void)
{
    connect_up();
    stage();
    authorize(0x11u);
    TEST_ASSERT_EQUAL(TFW_OK, serial(0x12u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_TRUE(marked());
}

static void test_a_request_off_the_ethernet_list_drops_the_hold(void)
{
    uint8_t p[2u + 4u] = { (uint8_t)TFW_NETCFG_ADDR, (uint8_t)(TFW_NETCFG_ADDR >> 8),
                           0xDEu, 0xADu, 0xBEu, 0xEFu };

    connect_up();
    stage();
    authorize(0x21u);
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, over_eth(0x22u, TFW_CMD_EEPROM_WRITE, p, (uint8_t)sizeof p, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, serial(0x23u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_FALSE(marked());
}

static void test_ota_activate_off_the_ethernet_list_does_not_take_the_hold(void)
{
    connect_up();
    stage();
    authorize(0x31u);
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, over_eth(0x32u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_FALSE(marked());
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, serial(0x33u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_FALSE(marked());
}

/* The ADR's open question 8, decided (ruling 109, Software 2026-10-26, #257):
 * an authorization ends with its connection, as well as at reset. OTA_AUTH is
 * off the Ethernet list (owner item 48), so a connection opens none, and the
 * next connection after the peer goes finds nothing held for it. */
static void test_an_ota_auth_over_ethernet_holds_nothing_past_its_connection(void)
{
    uint8_t sig[TFW_OTA_AUTH_SIG_LEN];

    memset(sig, 0xA5, sizeof sig);
    connect_up();
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, over_eth(0x71u, TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig, 0u));
    reconnect();
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, over_eth(0x72u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_FALSE(marked());
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, serial(0x73u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_FALSE(marked());
}

/* The serial link's authorization is its own: a TCP connection closing between
 * OTA_AUTH and OTA_ACTIVATE on serial dispatches nothing and drops nothing. */
static void test_a_tcp_connection_closing_leaves_the_serial_hold(void)
{
    connect_up();
    stage();
    authorize(0x81u);
    reconnect();
    TEST_ASSERT_EQUAL(TFW_OK, serial(0x82u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_TRUE(marked());
}

static void test_a_bad_crc_frame_over_serial_drops_the_hold(void)
{
    connect_up();
    stage();
    authorize(0x41u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRC, serial(0x42u, TFW_CMD_PING, NULL, 0u, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, serial(0x43u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_FALSE(marked());
}

static void test_a_bad_crc_frame_over_ethernet_drops_the_hold(void)
{
    connect_up();
    stage();
    authorize(0x51u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRC, over_eth(0x52u, TFW_CMD_OTA_STATUS, NULL, 0u, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, serial(0x53u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_FALSE(marked());
}

/* A bad-CRC OTA_AUTH frame holds nothing either. */
static void test_a_bad_crc_ota_auth_holds_nothing(void)
{
    uint8_t sig[TFW_OTA_AUTH_SIG_LEN];

    connect_up();
    stage();
    authorize(0x61u);
    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_ERR_CRC, serial(0x62u, TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, serial(0x63u, TFW_CMD_OTA_ACTIVATE, &M0, 1u, 0u));
    TEST_ASSERT_FALSE(marked());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ota_auth_then_activate_through_the_transport_marks_the_image);
    RUN_TEST(test_a_request_off_the_ethernet_list_drops_the_hold);
    RUN_TEST(test_ota_activate_off_the_ethernet_list_does_not_take_the_hold);
    RUN_TEST(test_an_ota_auth_over_ethernet_holds_nothing_past_its_connection);
    RUN_TEST(test_a_tcp_connection_closing_leaves_the_serial_hold);
    RUN_TEST(test_a_bad_crc_frame_over_serial_drops_the_hold);
    RUN_TEST(test_a_bad_crc_frame_over_ethernet_drops_the_hold);
    RUN_TEST(test_a_bad_crc_ota_auth_holds_nothing);
    return UNITY_END();
}
