/*
 * Seeded mutation of the provisioned network block (src/net/netcfg.c, the
 * layout in include/tfw/eth.h), on test_link_eth's link with the controller
 * faked as that suite fakes it: bit flips, all 0x00, all 0xFF, random bytes,
 * every truncation, every version and reserved byte, hostile fields under a
 * valid CRC, and the addresses at all 0x00, all 0xFF, a multicast MAC and a
 * broadcast IP. Every block goes through tfw_netcfg_decode, from a buffer of
 * exactly TFW_NETCFG_LEN bytes so an over-read is the sanitizer's, and through
 * tfw_netcfg_load from the EEPROM.
 *
 * The oracle is the layout as bytes: 'TFNC', version 1, a CRC over [0, 30)
 * little-endian at 30, then a non-zero peer and port. A block that is refused,
 * for its header or for a zero peer or port, leaves the caller's config
 * untouched byte for byte; one that is accepted decodes to exactly its bytes.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "crc16.h"
#include "hal_mock.h"
#include "tfw/eth.h"
#include "tfw/hal.h"

#define RANDOM_RUNS  3000u
#define HOSTILE_RUNS 3000u

/* The controller, as test_link_eth fakes it; nothing here starts it. */
static unsigned eth_starts;

tfw_status_t tfw_eth_start(const tfw_netcfg_t *cfg, uint16_t local_port)
{
    (void)cfg;
    (void)local_port;
    eth_starts++;
    return TFW_ERR_UNSUPPORTED;
}
uint8_t tfw_eth_state(void)        { return TFW_ETH_DOWN; }
uint8_t tfw_eth_rx(uint8_t *out)   { (void)out; return 0u; }
uint8_t tfw_eth_tx(uint8_t byte)   { (void)byte; return 0u; }
void    tfw_eth_send(void)         { }
void    tfw_eth_close(void)        { }

/* test_link_eth's literal block: 02:00:00:00:00:01, 192.0.2.10/24 via
 * 192.0.2.1, peer 192.0.2.20:7020. */
static const uint8_t NETCFG[TFW_NETCFG_LEN] = {
    'T', 'F', 'N', 'C', 0x01, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
    192, 0, 2, 10,   255, 255, 255, 0,   192, 0, 2, 1,   192, 0, 2, 20,
    0x1B, 0x6C,
    0x79, 0xF8,
};

static uint32_t rng = 0x4E7C0F15u;
static unsigned ok_runs, header_fails, endpoint_fails;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    tfw_mock_reset();
    ok_runs = header_fails = endpoint_fails = 0u;
}
void tearDown(void) { }

static void seal(uint8_t *raw)
{
    uint16_t crc = tfw_crc16(raw, 30u);

    raw[30] = (uint8_t)crc;
    raw[31] = (uint8_t)(crc >> 8);
}

static int header_ok(const uint8_t *raw)
{
    return raw[0] == 'T' && raw[1] == 'F' && raw[2] == 'N' && raw[3] == 'C'
        && raw[4] == 1u
        && tfw_crc16(raw, 30u) == (uint16_t)(raw[30] | (uint16_t)raw[31] << 8);
}

static int endpoint_ok(const uint8_t *raw)
{
    return (raw[28] | raw[29]) != 0u && (raw[24] | raw[25] | raw[26] | raw[27]) != 0u;
}

static void check_fields(const uint8_t *raw, const tfw_netcfg_t *c)
{
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&raw[6], c->mac, 6u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&raw[12], c->ip, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&raw[16], c->mask, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&raw[20], c->gateway, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&raw[24], c->peer, 4u);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(raw[28] << 8 | raw[29]), c->port);
}

static void check_one(tfw_status_t st, const uint8_t *raw, const tfw_netcfg_t *c,
                      const tfw_netcfg_t *untouched)
{
    if (!header_ok(raw) || !endpoint_ok(raw)) {
        TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, st);
        TEST_ASSERT_EQUAL_MEMORY(untouched, c, sizeof *c);
    } else {
        TEST_ASSERT_EQUAL(TFW_OK, st);
        check_fields(raw, c);
    }
}

/* One block through decode, from an exact-size heap copy, and through load. */
static void run(const uint8_t *raw)
{
    tfw_netcfg_t sentinel, c;
    uint8_t     *exact = malloc(TFW_NETCFG_LEN);

    TEST_ASSERT_NOT_NULL(exact);
    memcpy(exact, raw, TFW_NETCFG_LEN);
    memset(&sentinel, 0xA5, sizeof sentinel);

    c = sentinel;
    check_one(tfw_netcfg_decode(exact, &c), raw, &c, &sentinel);
    free(exact);

    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], raw, TFW_NETCFG_LEN);
    c = sentinel;
    check_one(tfw_netcfg_load(&c), raw, &c, &sentinel);

    if (!header_ok(raw)) {
        header_fails++;
    } else if (!endpoint_ok(raw)) {
        endpoint_fails++;
    } else {
        ok_runs++;
    }
}

static void test_every_bit_flip_is_not_provisioned(void)
{
    uint8_t  raw[TFW_NETCFG_LEN];
    unsigned bit;

    for (bit = 0u; bit < TFW_NETCFG_LEN * 8u; bit++) {
        memcpy(raw, NETCFG, sizeof raw);
        raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        run(raw);
    }
    TEST_ASSERT_EQUAL_UINT(TFW_NETCFG_LEN * 8u, header_fails);
}

static void test_every_bit_flip_under_a_resealed_crc(void)
{
    uint8_t  raw[TFW_NETCFG_LEN];
    unsigned bit;

    for (bit = 0u; bit < 30u * 8u; bit++) {
        memcpy(raw, NETCFG, sizeof raw);
        raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        seal(raw);
        run(raw);
    }
    /* Magic and version: 5 bytes. The rest decode, each to its own bits. */
    TEST_ASSERT_EQUAL_UINT(5u * 8u, header_fails);
    TEST_ASSERT_EQUAL_UINT(30u * 8u - 5u * 8u, ok_runs);
}

static void test_erased_and_zeroed_blocks(void)
{
    uint8_t raw[TFW_NETCFG_LEN];

    memset(raw, 0x00, sizeof raw);
    run(raw);
    memset(raw, 0xFF, sizeof raw);
    run(raw);
    seal(raw);
    run(raw);
    TEST_ASSERT_EQUAL_UINT(3u, header_fails);
}

/* A block written part of the way: the first n bytes, the rest erased or zero. */
static void test_every_truncation(void)
{
    uint8_t  raw[TFW_NETCFG_LEN];
    unsigned n;

    for (n = 0u; n <= TFW_NETCFG_LEN; n++) {
        memset(raw, 0xFF, sizeof raw);
        memcpy(raw, NETCFG, n);
        run(raw);
        memset(raw, 0x00, sizeof raw);
        memcpy(raw, NETCFG, n);
        run(raw);
    }
    /* Only the whole block, twice. */
    TEST_ASSERT_EQUAL_UINT(2u, ok_runs);
    TEST_ASSERT_EQUAL_UINT(2u * TFW_NETCFG_LEN, header_fails);
}

static void test_every_version_and_reserved_byte(void)
{
    uint8_t  raw[TFW_NETCFG_LEN];
    unsigned v;

    for (v = 0u; v < 256u; v++) {
        memcpy(raw, NETCFG, sizeof raw);
        raw[4] = (uint8_t)v;
        seal(raw);
        run(raw);
        memcpy(raw, NETCFG, sizeof raw);
        raw[5] = (uint8_t)v;
        seal(raw);
        run(raw);
    }
    TEST_ASSERT_EQUAL_UINT(255u, header_fails);
    TEST_ASSERT_EQUAL_UINT(257u, ok_runs);
}

static void test_random_blocks(void)
{
    uint8_t  raw[TFW_NETCFG_LEN];
    unsigned i, j;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        for (j = 0u; j < TFW_NETCFG_LEN; j++) {
            raw[j] = (uint8_t)xorshift32();
        }
        run(raw);
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, ok_runs + header_fails + endpoint_fails);
}

/* A right header, every field random, under a valid CRC. */
static void test_hostile_fields_under_a_valid_crc(void)
{
    uint8_t  raw[TFW_NETCFG_LEN];
    unsigned i, j;

    for (i = 0u; i < HOSTILE_RUNS; i++) {
        memcpy(raw, NETCFG, sizeof raw);
        for (j = 5u; j < 30u; j++) {
            raw[j] = (uint8_t)xorshift32();
        }
        seal(raw);
        run(raw);
    }
    TEST_ASSERT_EQUAL_UINT(0u, header_fails);
    TEST_ASSERT_EQUAL_UINT(HOSTILE_RUNS, ok_runs + endpoint_fails);
}

static void set_and_run(unsigned off, const uint8_t *v, unsigned n)
{
    uint8_t raw[TFW_NETCFG_LEN];

    memcpy(raw, NETCFG, sizeof raw);
    memcpy(&raw[off], v, n);
    seal(raw);
    run(raw);
}

/* The block carries addresses, not policy: an all-0x00 or all-0xFF MAC, IP,
 * mask or gateway, a multicast MAC and a broadcast IP or peer decode as
 * written. Only a zero peer or a zero port is "not provisioned". */
static void test_edge_addresses(void)
{
    static const uint8_t z[6] = { 0 }, f[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const uint8_t mcast[6] = { 0x01, 0x00, 0x5E, 0x00, 0x00, 0x01 };
    static const uint8_t bcast[4] = { 192, 0, 2, 255 };
    static const unsigned addr[] = { 12u, 16u, 20u };
    unsigned i;

    set_and_run(6u, z, 6u);
    set_and_run(6u, f, 6u);
    set_and_run(6u, mcast, 6u);
    for (i = 0u; i < sizeof addr / sizeof addr[0]; i++) {
        set_and_run(addr[i], z, 4u);
        set_and_run(addr[i], f, 4u);
        set_and_run(addr[i], bcast, 4u);
    }
    set_and_run(24u, f, 4u);
    set_and_run(24u, bcast, 4u);
    set_and_run(28u, f, 2u);
    TEST_ASSERT_EQUAL_UINT(3u + 9u + 3u, ok_runs);

    set_and_run(24u, z, 4u);
    set_and_run(28u, z, 2u);
    set_and_run(24u, z, 6u);
    TEST_ASSERT_EQUAL_UINT(3u, endpoint_fails);
}

static void test_a_refused_read_is_not_provisioned(void)
{
    tfw_netcfg_t sentinel, c;

    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], NETCFG, TFW_NETCFG_LEN);
    tfw_mock_eeprom_read_fails(1u);
    memset(&sentinel, 0xA5, sizeof sentinel);
    c = sentinel;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_load(&c));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &c, sizeof c);
}

/* After all of it, the literal block gives what test_link_eth pins. */
static void test_the_literal_block_afterwards(void)
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

    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], NETCFG, TFW_NETCFG_LEN);
    memset(&c, 0, sizeof c);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_netcfg_load(&c));
    TEST_ASSERT_EQUAL_UINT16(7020u, c.port);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(peer, c.peer, 4u);
    TEST_ASSERT_EQUAL_UINT(0u, eth_starts);
}

/* The two blocks that found the partial write: a valid magic, version and CRC
 * over the literal block with the peer zeroed, and with the port zeroed. */
static const uint8_t NETCFG_ZERO_PEER[TFW_NETCFG_LEN] = {
    0x54, 0x46, 0x4e, 0x43, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
    0xc0, 0x00, 0x02, 0x0a, 0xff, 0xff, 0xff, 0x00, 0xc0, 0x00, 0x02, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x1b, 0x6c, 0x82, 0x34
};
static const uint8_t NETCFG_ZERO_PORT[TFW_NETCFG_LEN] = {
    0x54, 0x46, 0x4e, 0x43, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
    0xc0, 0x00, 0x02, 0x0a, 0xff, 0xff, 0xff, 0x00, 0xc0, 0x00, 0x02, 0x01,
    0xc0, 0x00, 0x02, 0x14, 0x00, 0x00, 0xda, 0x8a
};

static void refused_untouched(const uint8_t *raw)
{
    tfw_netcfg_t sentinel, c;

    TEST_ASSERT_TRUE(header_ok(raw));
    TEST_ASSERT_FALSE(endpoint_ok(raw));
    memset(&sentinel, 0xA5, sizeof sentinel);

    c = sentinel;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_decode(raw, &c));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &c, sizeof c);

    memcpy(&tfw_mock_eeprom()[TFW_NETCFG_ADDR], raw, TFW_NETCFG_LEN);
    c = sentinel;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_load(&c));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &c, sizeof c);
    TEST_ASSERT_EQUAL_UINT(0u, eth_starts);
}

static void test_a_zero_peer_leaves_the_config_untouched(void)
{
    refused_untouched(NETCFG_ZERO_PEER);
}

static void test_a_zero_port_leaves_the_config_untouched(void)
{
    refused_untouched(NETCFG_ZERO_PORT);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_bit_flip_is_not_provisioned);
    RUN_TEST(test_every_bit_flip_under_a_resealed_crc);
    RUN_TEST(test_erased_and_zeroed_blocks);
    RUN_TEST(test_every_truncation);
    RUN_TEST(test_every_version_and_reserved_byte);
    RUN_TEST(test_random_blocks);
    RUN_TEST(test_hostile_fields_under_a_valid_crc);
    RUN_TEST(test_edge_addresses);
    RUN_TEST(test_a_refused_read_is_not_provisioned);
    RUN_TEST(test_the_literal_block_afterwards);
    RUN_TEST(test_a_zero_peer_leaves_the_config_untouched);
    RUN_TEST(test_a_zero_port_leaves_the_config_untouched);
    return UNITY_END();
}
