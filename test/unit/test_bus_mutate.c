/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the I2C, SPI and 1-Wire commands.
 *
 * test_bus pins what a clean request does. This feeds the same requests,
 * through the same dispatcher, mutated: bit flips, truncation at every length,
 * all 0x00, all 0xFF, random bytes, and every address, length, pin and field
 * swept through 0, 1, the maximum, one past it and wrapped. Each payload sits
 * in a heap buffer of exactly its length; each reply goes to a writer of a
 * chosen capacity, some bytes already in it, with guard bytes after it. Every
 * call is held to:
 *
 *   - nothing past the writer's capacity, nothing over the bytes it held;
 *   - a refusal leaves the writer's length where it was and the HAL unreached;
 *   - a transfer answers exactly the bytes it asked for, from the bus, never
 *     more than the request or the writer can carry;
 *   - the secure element's address never reaches the I2C HAL, and a reserved
 *     pin never carries a 1-Wire transfer.
 *
 * Deterministic: xorshift32 from a literal seed, literal counts. A failure
 * prints the command and the payload so the case can be pinned.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "bus.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

#define GUARD       16u
#define GUARD_BYTE  0xA5u
#define PRE_BYTE    0xC3u
#define LIMIT       (TFW_MAX_PAYLOAD + 1u)
#define BUS_MAX     (TFW_MAX_PAYLOAD - 1u) /* TFW_BUS_MAX, private to src/caps/bus.c */
#define RANDOM_RUNS 1000u                  /* per command */

static uint32_t rng;
static uint8_t  resp[64];
static unsigned ran;
static unsigned ok_runs;
static unsigned refused_runs;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    unsigned i;

    tfw_mock_reset();
    for (i = 0u; i < sizeof resp; i++) {
        resp[i] = (uint8_t)(i * 13u + 5u);
    }
    tfw_mock_set_bus_response(resp, (uint8_t)sizeof resp);
    tfw_mock_ow_presence_set(1u);
}
void tearDown(void) {}

static void start(uint32_t seed)
{
    rng          = seed;
    ran          = 0u;
    ok_runs      = 0u;
    refused_runs = 0u;
}

static void report(const char *what)
{
    printf("%s: %u requests, %u ok, %u refused\n", what, ran, ok_runs, refused_runs);
}

static void dump(uint8_t cmd, const uint8_t *p, unsigned n, unsigned cap, unsigned pre)
{
    unsigned i;
    printf("cmd 0x%02x cap %u pre %u payload[%u]:", cmd, cap, pre, n);
    for (i = 0u; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

static uint8_t reserved_pin(uint8_t pin)
{
    return (uint8_t)((tfw_board_pin_flags(pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) != 0u);
}

/* A transfer's reply: exactly want bytes of the bus response, or TOO_LONG
 * because they did not fit. */
static const char *check_bytes(tfw_status_t st, const uint8_t *reply, unsigned rlen,
                               unsigned room, unsigned want)
{
    if (want > BUS_MAX) {
        return "a transfer longer than a reply can carry was accepted";
    }
    if (st == TFW_ERR_TOO_LONG) {
        return want > room ? NULL : "a transfer that fitted was called too long";
    }
    if (rlen != want) {
        return "a transfer answered a different length than it asked";
    }
    if (rlen && memcmp(reply, resp, rlen) != 0) {
        return "a transfer answered bytes the bus did not give";
    }
    return NULL;
}

/* What the call must have done, given the request and its status; NULL when
 * it did. */
static const char *check(uint8_t cmd, const uint8_t *p, unsigned n, tfw_status_t st,
                         const uint8_t *reply, unsigned rlen, unsigned room)
{
    const uint8_t addr  = tfw_mock_last_i2c_addr();
    const uint8_t taken = (uint8_t)(st == TFW_OK || st == TFW_ERR_TOO_LONG);

    if (addr == TFW_I2C_RESERVED_ADDR || addr == TFW_ELEMENT_I2C_ADDR) {
        return "the secure element's address reached the I2C HAL";
    }
    if (cmd != TFW_CMD_I2C_XFER && addr != 0xFFu) {
        return "a command that is not an I2C transfer reached the I2C HAL";
    }
    if (cmd != TFW_CMD_I2C_CONFIG && tfw_mock_last_i2c_hz() != 0xFFFFFFFFuL) {
        return "a command that is not I2C_CONFIG reached tfw_hal_i2c_init";
    }
    if (cmd != TFW_CMD_SPI_CONFIG && tfw_mock_last_spi_mode() != 0xFFu) {
        return "a command that is not SPI_CONFIG reached tfw_hal_spi_init";
    }
    if (!taken) {
        if (cmd == TFW_CMD_I2C_XFER && st != TFW_ERR_NACK && addr != 0xFFu) {
            return "a refused I2C transfer reached the HAL";
        }
        if (cmd == TFW_CMD_I2C_CONFIG && tfw_mock_last_i2c_hz() != 0xFFFFFFFFuL) {
            return "a refused I2C_CONFIG reached the HAL";
        }
        if (cmd == TFW_CMD_SPI_CONFIG && tfw_mock_last_spi_mode() != 0xFFu) {
            return "a refused SPI_CONFIG reached the HAL";
        }
        return NULL;
    }
    switch (cmd) {
    case TFW_CMD_I2C_XFER:
        if (n < 2u || p[0] > 0x7Fu || addr != p[0]) {
            return "an I2C transfer outside the contract was accepted";
        }
        return check_bytes(st, reply, rlen, room, p[1]);
    case TFW_CMD_I2C_CONFIG: {
        const uint32_t hz = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
                          | ((uint32_t)p[3] << 24);
        if (n != 4u || hz < TFW_I2C_HZ_MIN || hz > TFW_I2C_HZ_MAX || tfw_mock_last_i2c_hz() != hz) {
            return "an I2C clock outside the contract was accepted";
        }
        return rlen ? "I2C_CONFIG answered bytes" : NULL;
    }
    case TFW_CMD_SPI_XFER:
        if (n < 2u || reserved_pin(p[0])) {
            return "an SPI transfer outside the contract was accepted";
        }
        return check_bytes(st, reply, rlen, room, n - 1u);
    case TFW_CMD_SPI_CONFIG:
        if (n != 3u || p[0] > 3u || p[1] > 3u || p[2] > 1u
            || tfw_mock_last_spi_mode() != p[0] || tfw_mock_last_spi_clkdiv() != p[1]
            || tfw_mock_last_spi_lsb() != p[2]) {
            return "an SPI configuration outside the contract was accepted";
        }
        return rlen ? "SPI_CONFIG answered bytes" : NULL;
    case TFW_CMD_ONEWIRE_RESET:
        if (n != 1u || reserved_pin(p[0])) {
            return "a 1-Wire reset on a reserved pin was accepted";
        }
        if (st == TFW_ERR_TOO_LONG) {
            return room < 1u ? NULL : "a presence byte that fitted was called too long";
        }
        return (rlen == 1u && reply[0] == 1u) ? NULL : "a 1-Wire reset did not answer the presence";
    case TFW_CMD_ONEWIRE_WRITE:
        if (n < 2u || reserved_pin(p[0])) {
            return "a 1-Wire write on a reserved pin was accepted";
        }
        return rlen ? "a 1-Wire write answered bytes" : NULL;
    case TFW_CMD_ONEWIRE_READ:
        if (n != 2u || p[1] == 0u || reserved_pin(p[0])) {
            return "a 1-Wire read outside the contract or on a reserved pin was accepted";
        }
        return check_bytes(st, reply, rlen, room, p[1]);
    default:
        return "an unexpected command";
    }
}

static tfw_status_t feed_at(uint8_t cmd, const uint8_t *p, unsigned n, unsigned cap, unsigned pre)
{
    uint8_t      *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t      *out = (uint8_t *)malloc(cap + GUARD);
    tfw_reader_t  r;
    tfw_writer_t  w;
    tfw_status_t  st;
    const char   *fail = NULL;
    unsigned      len;
    unsigned      i;

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    if (n) { memcpy(in, p, n); }
    memset(out, GUARD_BYTE, cap + GUARD);
    setUp();

    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)cap);
    for (i = 0u; i < pre; i++) {
        tfw_write_u8(&w, PRE_BYTE);
    }
    st  = tfw_dispatch(cmd, &r, &w);
    len = tfw_writer_len(&w);

    for (i = cap; i < cap + GUARD && !fail; i++) {
        if (out[i] != GUARD_BYTE) {
            fail = "wrote past the writer's capacity";
        }
    }
    for (i = 0u; i < pre && !fail; i++) {
        if (out[i] != PRE_BYTE) {
            fail = "overwrote bytes the writer already held";
        }
    }
    if (!fail && len > cap) {
        fail = "the writer's length ran past its capacity";
    }
    if (!fail && (unsigned)st > (unsigned)TFW_ERR_NO_STORE) {
        fail = "an unknown status";
    }
    if (!fail && st != TFW_OK && st != TFW_ERR_TOO_LONG && len != pre) {
        fail = "a refused request moved the writer";
    }
    if (!fail && n > TFW_MAX_PAYLOAD && st != TFW_ERR_BAD_ARGC) {
        fail = "a request past the frame maximum was not refused";
    }
    if (!fail) {
        fail = check(cmd, p, n, st, out + pre, len - pre, cap - pre);
    }
    if (fail) {
        printf("status %u, writer %u\n", (unsigned)st, len);
        dump(cmd, p, n, cap, pre);
        TEST_FAIL_MESSAGE(fail);
    }
    ran++;
    if (st == TFW_OK) {
        ok_runs++;
    } else {
        refused_runs++;
    }
    free(in);
    free(out);
    return st;
}

/* Each request twice: into a whole reply buffer, then into a random shorter
 * one that already holds a byte or two. */
static tfw_status_t feed(uint8_t cmd, const uint8_t *p, unsigned n)
{
    const tfw_status_t st  = feed_at(cmd, p, n, TFW_MAX_PAYLOAD, 0u);
    const unsigned     cap = rnd() % (TFW_MAX_PAYLOAD + 1u);
    (void)feed_at(cmd, p, n, cap, cap ? rnd() % (cap < 3u ? cap + 1u : 3u) : 0u);
    return st;
}

static void random_fill(uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)rnd();
    }
}

/* The seeds are the requests test_bus sends. */
static const uint8_t i2c_xfer_seed[3]   = { 0x48u, 3u, 0x00u };
static const uint8_t i2c_config_seed[4] = { 0x80u, 0x1Au, 0x06u, 0x00u };   /* 400 kHz */
static const uint8_t spi_xfer_seed[3]   = { 10u, 0xAAu, 0xBBu };
static const uint8_t spi_config_seed[3] = { 3u, 2u, 1u };
static const uint8_t ow_reset_seed[1]   = { 9u };
static const uint8_t ow_write_seed[3]   = { 9u, 0xCCu, 0xBEu };
static const uint8_t ow_read_seed[2]    = { 9u, 2u };

/* The values a length goes wrong at, around the transfer maximum. */
static const unsigned lens[] = { 0u, 1u, 2u, BUS_MAX - 1u, BUS_MAX, BUS_MAX + 1u, BUS_MAX + 2u,
                                 0x7Fu, 0x80u, 0xFEu, 0xFFu };
#define NLENS (sizeof lens / sizeof lens[0])

static void test_every_i2c_address_against_the_read_and_write_lengths(void)
{
    static const unsigned wlens[] = { 0u, 1u, TFW_MAX_PAYLOAD - 3u, TFW_MAX_PAYLOAD - 2u };
    uint8_t  p[TFW_MAX_PAYLOAD];
    unsigned a;
    unsigned l;
    unsigned k;

    start(0x9E3779B9u);
    for (a = 0u; a < 256u; a++) {
        for (l = 0u; l < NLENS; l++) {
            for (k = 0u; k < 4u; k++) {
                p[0] = (uint8_t)a;
                p[1] = (uint8_t)lens[l];
                random_fill(&p[2], wlens[k]);
                feed(TFW_CMD_I2C_XFER, p, 2u + wlens[k]);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 256u * NLENS * 4u, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("i2c xfer fields");
}

static void test_every_i2c_clock_edge(void)
{
    static const uint32_t hz[] = {
        0u, 1u, TFW_I2C_HZ_MIN - 1u, TFW_I2C_HZ_MIN, TFW_I2C_HZ_MIN + 1u, 100000u,
        TFW_I2C_HZ_MAX - 1u, TFW_I2C_HZ_MAX, TFW_I2C_HZ_MAX + 1u, 0x7FFFFFFFu, 0x80000000u,
        0xFFFFFFFFu, 0xFFFFFFFFu - TFW_I2C_HZ_MAX,
    };
    uint8_t  p[4];
    unsigned i;

    start(0x7F4A7C15u);
    for (i = 0u; i < sizeof hz / sizeof hz[0]; i++) {
        p[0] = (uint8_t)hz[i];
        p[1] = (uint8_t)(hz[i] >> 8);
        p[2] = (uint8_t)(hz[i] >> 16);
        p[3] = (uint8_t)(hz[i] >> 24);
        feed(TFW_CMD_I2C_CONFIG, p, 4u);
    }
    TEST_ASSERT_EQUAL_UINT(2u * (sizeof hz / sizeof hz[0]), ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("i2c config fields");
}

static void test_every_chip_select_against_every_length(void)
{
    uint8_t  p[LIMIT];
    unsigned cs;
    unsigned n;

    start(0x2545F491u);
    for (cs = 0u; cs < 256u; cs++) {
        for (n = 1u; n <= LIMIT; n++) {
            p[0] = (uint8_t)cs;
            random_fill(&p[1], n - 1u);
            feed(TFW_CMD_SPI_XFER, p, n);
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 256u * LIMIT, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("spi xfer fields");
}

static void test_every_spi_configuration_edge(void)
{
    static const uint8_t v[] = { 0u, 1u, 2u, 3u, 4u, 0x7Fu, 0x80u, 0xFFu };
    uint8_t  p[3];
    unsigned a;
    unsigned b;
    unsigned c;

    start(0x6C8E9CF5u);
    for (a = 0u; a < sizeof v; a++) {
        for (b = 0u; b < sizeof v; b++) {
            for (c = 0u; c < sizeof v; c++) {
                p[0] = v[a];
                p[1] = v[b];
                p[2] = v[c];
                feed(TFW_CMD_SPI_CONFIG, p, 3u);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 8u * 8u * 8u, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("spi config fields");
}

static void test_every_onewire_pin_against_every_length(void)
{
    uint8_t  p[LIMIT];
    unsigned pin;
    unsigned n;

    start(0x1B873593u);
    for (pin = 0u; pin < 256u; pin++) {
        p[0] = (uint8_t)pin;
        feed(TFW_CMD_ONEWIRE_RESET, p, 1u);
        for (n = 0u; n < 256u; n++) {
            p[1] = (uint8_t)n;
            feed(TFW_CMD_ONEWIRE_READ, p, 2u);
        }
        for (n = 2u; n <= LIMIT; n++) {
            random_fill(&p[1], n - 1u);
            feed(TFW_CMD_ONEWIRE_WRITE, p, n);
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 256u * (1u + 256u + LIMIT - 1u), ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("1-wire fields");
}

static unsigned mutate(uint8_t cmd, const uint8_t *seed, unsigned sn)
{
    uint8_t  buf[LIMIT];
    unsigned n;
    unsigned i;
    unsigned k;
    unsigned b;

    /* truncation at every length, and every length up to one past the frame */
    for (n = 0u; n <= LIMIT; n++) {
        memcpy(buf, seed, n < sn ? n : sn);
        if (n > sn) {
            random_fill(buf + sn, n - sn);
        }
        feed(cmd, buf, n);
    }
    /* all 0x00 and all 0xFF at every length */
    for (n = 0u; n <= LIMIT; n++) {
        memset(buf, 0x00, n);
        feed(cmd, buf, n);
        memset(buf, 0xFF, n);
        feed(cmd, buf, n);
    }
    /* every single bit flipped */
    for (b = 0u; b < sn * 8u; b++) {
        memcpy(buf, seed, sn);
        buf[b / 8u] ^= (uint8_t)(1u << (b % 8u));
        feed(cmd, buf, sn);
    }
    /* several bits, then a random cut or a random tail */
    for (k = 0u; k < RANDOM_RUNS; k++) {
        unsigned flips = 1u + rnd() % 4u;
        n = sn;
        memcpy(buf, seed, sn);
        while (flips--) {
            const uint32_t at = rnd() % (n * 8u);
            buf[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        }
        switch (rnd() % 3u) {
        case 0u:
            n = rnd() % (n + 1u);
            break;
        case 1u:
            i = rnd() % (LIMIT - n + 1u);
            random_fill(buf + n, i);
            n += i;
            break;
        default:
            break;
        }
        feed(cmd, buf, n);
    }
    /* pure random payloads of every length */
    for (n = 0u; n <= LIMIT; n++) {
        for (k = 0u; k < 4u; k++) {
            random_fill(buf, n);
            feed(cmd, buf, n);
        }
    }
    return 2u * ((LIMIT + 1u) * 3u + sn * 8u + RANDOM_RUNS + (LIMIT + 1u) * 4u);
}

static void test_mutated_bus_requests(void)
{
    unsigned want = 0u;

    start(0x85EBCA6Bu);
    want += mutate(TFW_CMD_I2C_XFER, i2c_xfer_seed, sizeof i2c_xfer_seed);
    want += mutate(TFW_CMD_I2C_CONFIG, i2c_config_seed, sizeof i2c_config_seed);
    want += mutate(TFW_CMD_SPI_XFER, spi_xfer_seed, sizeof spi_xfer_seed);
    want += mutate(TFW_CMD_SPI_CONFIG, spi_config_seed, sizeof spi_config_seed);
    want += mutate(TFW_CMD_ONEWIRE_RESET, ow_reset_seed, sizeof ow_reset_seed);
    want += mutate(TFW_CMD_ONEWIRE_WRITE, ow_write_seed, sizeof ow_write_seed);
    want += mutate(TFW_CMD_ONEWIRE_READ, ow_read_seed, sizeof ow_read_seed);
    TEST_ASSERT_EQUAL_UINT(want, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("mutated bus requests");
}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n, uint8_t *out)
{
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, p, n);
    tfw_writer_init(&w, out, TFW_MAX_PAYLOAD);
    return tfw_dispatch(cmd, &r, &w);
}

/* After all that, the clean requests answer as test_bus pins them. */
static void test_the_clean_requests_afterwards(void)
{
    const uint8_t i2c_resp[3]   = { 0xAA, 0xBB, 0xCC };
    const uint8_t scratchpad[2] = { 0x50, 0x05 };
    uint8_t       out[TFW_MAX_PAYLOAD];

    tfw_mock_reset();
    tfw_mock_set_bus_response(i2c_resp, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_I2C_XFER, i2c_xfer_seed, 3u, out));
    TEST_ASSERT_EQUAL_UINT8(0x48u, tfw_mock_last_i2c_addr());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(i2c_resp, out, 3u);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION,
        run(TFW_CMD_I2C_XFER, (const uint8_t[]){ 0x60u, 1u, 0x00u }, 3u, out));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_I2C_CONFIG, i2c_config_seed, 4u, out));
    TEST_ASSERT_EQUAL_UINT32(400000uL, tfw_mock_last_i2c_hz());
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SPI_CONFIG, spi_config_seed, 3u, out));
    TEST_ASSERT_EQUAL_UINT8(3u, tfw_mock_last_spi_mode());
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_mock_last_spi_clkdiv());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_last_spi_lsb());

    tfw_mock_set_bus_response((const uint8_t[]){ 0x12, 0x34 }, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SPI_XFER, spi_xfer_seed, 3u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(((const uint8_t[]){ 0x12, 0x34 }), out, 2u);

    tfw_mock_ow_presence_set(1u);
    tfw_mock_set_bus_response(scratchpad, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ONEWIRE_RESET, ow_reset_seed, 1u, out));
    TEST_ASSERT_EQUAL_UINT8(1u, out[0]);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ONEWIRE_WRITE, ow_write_seed, 3u, out));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ONEWIRE_READ, ow_read_seed, 2u, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(scratchpad, out, 2u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_i2c_address_against_the_read_and_write_lengths);
    RUN_TEST(test_every_i2c_clock_edge);
    RUN_TEST(test_every_chip_select_against_every_length);
    RUN_TEST(test_every_spi_configuration_edge);
    RUN_TEST(test_every_onewire_pin_against_every_length);
    RUN_TEST(test_mutated_bus_requests);
    RUN_TEST(test_the_clean_requests_afterwards);
    return UNITY_END();
}
