/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * CAPS, PING, RESET and SYS_INFO fed mutated requests (#163): called directly
 * through the reader and writer exactly as test_sys does, and again through
 * tfw_dispatch so the commands.def arity holds in front of them. Every length,
 * every bit flip of a clean request, every pin and mode byte, every writer
 * capacity, and random bytes. The writer's buffer carries guard bytes past its
 * capacity; a refused request writes nothing, latches no reset and touches no
 * pin and no handoff byte.
 *
 * xorshift32 with a literal seed: a failure reproduces from the iteration.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "sys.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/board.h"
#include "tfw/boot.h"

#define GUARD       16u
#define RANDOM_RUNS 3000u
#define CAPS_LEN    16u
#define INFO_LEN    15u

typedef tfw_status_t (*handler_t)(tfw_reader_t *r, tfw_writer_t *w);

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD + GUARD];
static tfw_reader_t r;
static tfw_writer_t w;

static uint32_t rng = 0x5A5C0DE5u;
static unsigned runs, refused;

static const uint8_t   CMDS[] = { TFW_CMD_CAPS, TFW_CMD_PING, TFW_CMD_RESET, TFW_CMD_SYS_INFO };
static const handler_t FNS[]  = { tfw_sys_cmd_caps, tfw_sys_cmd_ping, tfw_sys_cmd_reset,
                                  tfw_sys_cmd_sys_info };
static const uint8_t   MINS[] = { 0u, 0u, 1u, 0u };
static const uint8_t   MAXS[] = { 1u, TFW_MAX_PAYLOAD, 1u, 0u };
static const uint8_t   CAPS[] = { 0u, 1u, 5u, 6u, 7u, 14u, 15u, 16u, 17u, 63u, TFW_MAX_PAYLOAD };

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
    tfw_sys_reset_state();
    runs = refused = 0u;
}
void tearDown(void) {}

static int handoff_touched(void)
{
    unsigned i;

    for (i = 0u; i < TFW_BOOT_REQUEST_LEN; i++) {
        if (tfw_mock_handoff()[i] != 0u) {
            return 1;
        }
    }
    return 0;
}

static void caps_header(uint8_t *want)
{
    uint32_t bitmap = TFW_FEATURE_BITMAP;
    uint16_t algs   = tfw_crypto_algorithms();

    want[0]  = TFW_PROTO_VERSION_MAJOR;
    want[1]  = TFW_PROTO_VERSION_MINOR;
    want[2]  = 0u;                       /* TFW_FW_MAJOR, sys.c:16 */
    want[3]  = 1u;                       /* TFW_FW_MINOR, sys.c:17 */
    want[4]  = 0u;                       /* TFW_FW_PATCH, sys.c:18 */
    want[5]  = TFW_BOARD_ID;
    want[6]  = TFW_PIN_COUNT;
    want[7]  = (uint8_t)TFW_SCHED_SLOTS;
    want[8]  = (uint8_t)TFW_MAX_PAYLOAD;
    want[9]  = (uint8_t)tfw_crypto_tier();
    want[10] = (uint8_t)bitmap;
    want[11] = (uint8_t)(bitmap >> 8);
    want[12] = (uint8_t)(bitmap >> 16);
    want[13] = (uint8_t)(bitmap >> 24);
    want[14] = (uint8_t)algs;
    want[15] = (uint8_t)(algs >> 8);
}

/* The reply the handler builds, its length, and its status, for a direct call
 * with n bytes -- sys.c reads at most what is there and a missing mode byte
 * reads as 0. */
static tfw_status_t model(unsigned c, const uint8_t *p, uint8_t n, uint8_t *want, uint8_t *len)
{
    *len = 0u;
    if (c == 0u) {
        if (n == 1u) {
            if (p[0] >= TFW_PIN_COUNT) {
                return TFW_ERR_BAD_PIN;
            }
            TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin_page(p[0], want));
            *len = TFW_PIN_PAGE_LEN;
            return TFW_OK;
        }
        caps_header(want);
        *len = CAPS_LEN;
        return TFW_OK;
    }
    if (c == 1u) {
        if (n) { memcpy(want, p, n); }
        *len = n;
        return TFW_OK;
    }
    if (c == 2u) {
        return n && p[0] > 1u ? TFW_ERR_RANGE : TFW_OK;
    }
    /* SYS_INFO from the mock's reset state: millis 0, the mock's free RAM,
     * its reset cause, and framer counters cleared by tfw_sys_reset_state. */
    {
        uint32_t ram = tfw_hal_free_ram();

        memset(want, 0, INFO_LEN);
        want[4]  = ram > 0xFFFFu ? 0xFFu : (uint8_t)ram;
        want[5]  = ram > 0xFFFFu ? 0xFFu : (uint8_t)(ram >> 8);
        want[6]  = (uint8_t)tfw_hal_reset_cause();
        want[11] = (uint8_t)ram;
        want[12] = (uint8_t)(ram >> 8);
        want[13] = (uint8_t)(ram >> 16);
        want[14] = (uint8_t)(ram >> 24);
        *len     = INFO_LEN;
        return TFW_OK;
    }
}

/* One request, direct (via = 0) or through tfw_dispatch (via = 1), into a
 * writer of capacity cap. */
static void check_one(unsigned c, const uint8_t *p, uint8_t n, uint8_t cap, unsigned via)
{
    uint8_t      want[TFW_MAX_PAYLOAD];
    uint8_t      len, mode;
    tfw_status_t st, expect;
    unsigned     i;

    tfw_mock_reset();
    tfw_sys_reset_state();
    expect = model(c, p, n, want, &len);
    if (via && (n < MINS[c] || n > MAXS[c])) {
        expect = TFW_ERR_BAD_ARGC;
        len    = 0u;
    }
    if (expect == TFW_OK && len > cap) {
        /* Direct, the handler answers OK into a sticky overflow; dispatch
         * turns that into TOO_LONG. */
        expect = via ? TFW_ERR_TOO_LONG : TFW_OK;
    }

    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    memset(outbuf, 0xEE, sizeof outbuf);
    tfw_writer_init(&w, outbuf, cap);
    st = via ? tfw_dispatch(CMDS[c], &r, &w) : FNS[c](&r, &w);
    TEST_ASSERT_EQUAL(expect, st);
    for (i = cap; i < sizeof outbuf; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xEEu, outbuf[i]);
    }
    TEST_ASSERT_TRUE(tfw_writer_len(&w) <= cap);
    runs++;

    if (len > cap) {
        /* At a short capacity only the status and the guard are this test's
         * contract: no reply body is sent with TOO_LONG. */
        TEST_ASSERT_FALSE(tfw_writer_ok(&w));
        return;
    }
    if (st != TFW_OK) {
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
        TEST_ASSERT_FALSE(tfw_sys_reset_pending());
        tfw_sys_poll();
        TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
        TEST_ASSERT_FALSE(handoff_touched());
        refused++;
        return;
    }
    TEST_ASSERT_TRUE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(len, tfw_writer_len(&w));
    if (len) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, outbuf, len);
    }
    if (c != 2u) {
        TEST_ASSERT_FALSE(tfw_sys_reset_pending());
        TEST_ASSERT_FALSE(handoff_touched());
        return;
    }
    /* An accepted RESET latches; the poll resets once, and mode 1 alone arms
     * the boot request. */
    mode = n ? p[0] : 0u;
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_EQUAL(mode == 1u, handoff_touched());
}

static void check(unsigned c, const uint8_t *p, uint8_t n)
{
    check_one(c, p, n, (uint8_t)TFW_MAX_PAYLOAD, 0u);
    check_one(c, p, n, (uint8_t)TFW_MAX_PAYLOAD, 1u);
}

static uint8_t clean_request(unsigned c, uint8_t *p)
{
    static const uint8_t PING[] = { 0xDEu, 0xADu, 0xBEu, 0xEFu };

    if (c == 0u) { p[0] = 0u; return 1u; }
    if (c == 1u) { memcpy(p, PING, sizeof PING); return (uint8_t)sizeof PING; }
    if (c == 2u) { p[0] = 1u; return 1u; }
    return 0u;
}

static void test_every_length_of_every_command(void)
{
    uint8_t  p[TFW_MAX_PAYLOAD];
    unsigned c, n, fill, j;

    for (c = 0u; c < sizeof CMDS; c++) {
        for (fill = 0u; fill < 3u; fill++) {
            for (n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
                for (j = 0u; j < sizeof p; j++) {
                    p[j] = fill == 0u ? 0x00u : fill == 1u ? 0xFFu : (uint8_t)xorshift32();
                }
                check(c, p, (uint8_t)n);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(sizeof CMDS * 3u * (TFW_MAX_PAYLOAD + 1u) * 2u, runs);
    TEST_ASSERT_TRUE(refused > 0u);
}

static void test_every_bit_flip_of_a_clean_request(void)
{
    uint8_t  base[4], p[4];
    unsigned c, bit, n, total = 0u;

    for (c = 0u; c < sizeof CMDS; c++) {
        n = clean_request(c, base);
        check(c, base, (uint8_t)n);
        total++;
        for (bit = 0u; bit < n * 8u; bit++) {
            memcpy(p, base, sizeof p);
            p[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            check(c, p, (uint8_t)n);
            total++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(4u + 8u + 32u + 8u, total);
    TEST_ASSERT_EQUAL_UINT(total * 2u, runs);
}

/* Every value of the one-byte fields: the CAPS pin and the RESET mode, which
 * covers 0, 1, the maximum, the maximum + 1 and the wrap of each. */
static void test_every_pin_and_mode_byte(void)
{
    uint8_t  b;
    unsigned v;

    for (v = 0u; v < 256u; v++) {
        b = (uint8_t)v;
        check(0u, &b, 1u);
        check(2u, &b, 1u);
    }
    TEST_ASSERT_EQUAL_UINT(256u * 2u * 2u, runs);
    TEST_ASSERT_EQUAL_UINT((256u - TFW_PIN_COUNT + 254u) * 2u, refused);
}

/* Every reply against writer capacities around its length: nothing past the
 * capacity, and dispatch answers TOO_LONG where it does not fit. */
static void test_every_writer_capacity(void)
{
    uint8_t  p[TFW_MAX_PAYLOAD];
    unsigned c, k, j, n;

    for (j = 0u; j < sizeof p; j++) {
        p[j] = (uint8_t)xorshift32();
    }
    for (c = 0u; c < sizeof CMDS; c++) {
        for (k = 0u; k < sizeof CAPS; k++) {
            n = clean_request(c, p);
            check_one(c, p, (uint8_t)n, CAPS[k], 0u);
            check_one(c, p, (uint8_t)n, CAPS[k], 1u);
            if (c == 1u) {
                check_one(c, p, (uint8_t)TFW_MAX_PAYLOAD, CAPS[k], 0u);
                check_one(c, p, (uint8_t)TFW_MAX_PAYLOAD, CAPS[k], 1u);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT((sizeof CMDS + 1u) * sizeof CAPS * 2u, runs);
}

static void test_named_pin_edges(void)
{
    const uint8_t edges[] = { (uint8_t)TFW_PIN_COUNT, (uint8_t)(TFW_PIN_COUNT + 1u), 0xFFu };
    unsigned      e;

    for (e = 0u; e < sizeof edges; e++) {
        tfw_reader_init(&r, &edges[e], 1u);
        tfw_writer_init(&w, outbuf, (uint8_t)TFW_MAX_PAYLOAD);
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_sys_cmd_caps(&r, &w));
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    }
}

static void test_random_requests(void)
{
    uint8_t  p[TFW_MAX_PAYLOAD];
    unsigned i, j;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        uint32_t x = xorshift32();
        unsigned c = x % sizeof CMDS;
        uint8_t  n = MINS[c];

        for (j = 0u; j < sizeof p; j++) {
            p[j] = (uint8_t)xorshift32();
        }
        /* Mostly in arity, so the handlers past dispatch run. */
        if ((x >> 8) % 4u == 0u) {
            n = (uint8_t)((x >> 16) % (TFW_MAX_PAYLOAD + 1u));
        } else if (MAXS[c] > MINS[c]) {
            n = (uint8_t)(MINS[c] + (x >> 16) % (MAXS[c] - MINS[c] + 1u));
        }
        check(c, p, n);
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS * 2u, runs);
}

/* After all of it, clean requests give what test_sys pins. */
static void test_clean_requests_afterwards(void)
{
    static const uint8_t info[INFO_LEN] = {
        0x78u, 0x56u, 0x34u, 0x12u, 0xFFu, 0xFFu, (uint8_t)TFW_RESET_WATCHDOG,
        0x34u, 0x12u, 0xCDu, 0xABu, 0x45u, 0x23u, 0x01u, 0x00u,
    };
    uint8_t want[CAPS_LEN];
    uint8_t b;

    tfw_mock_reset();
    tfw_sys_reset_state();
    caps_header(want);
    tfw_reader_init(&r, inbuf, 0u);
    tfw_writer_init(&w, outbuf, (uint8_t)TFW_MAX_PAYLOAD);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_caps(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(CAPS_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, outbuf, CAPS_LEN);

    b = 2u;
    tfw_reader_init(&r, &b, 1u);
    tfw_writer_init(&w, outbuf, (uint8_t)TFW_MAX_PAYLOAD);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_sys_cmd_reset(&r, &w));
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());

    b = 0u;
    tfw_reader_init(&r, &b, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_reset(&r, &w));
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_FALSE(handoff_touched());

    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_mock_set_millis(0x12345678u);
    tfw_mock_set_free_ram(0x00012345u);
    tfw_mock_set_reset_cause(TFW_RESET_WATCHDOG);
    tfw_sys_set_framer_errors(0x1234u, 0xABCDu);
    tfw_reader_init(&r, inbuf, 0u);
    tfw_writer_init(&w, outbuf, (uint8_t)TFW_MAX_PAYLOAD);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_sys_info(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(INFO_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(info, outbuf, INFO_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_length_of_every_command);
    RUN_TEST(test_every_bit_flip_of_a_clean_request);
    RUN_TEST(test_every_pin_and_mode_byte);
    RUN_TEST(test_every_writer_capacity);
    RUN_TEST(test_named_pin_edges);
    RUN_TEST(test_random_requests);
    RUN_TEST(test_clean_requests_afterwards);
    return UNITY_END();
}
