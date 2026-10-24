/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * PIN_MODE, DIGITAL_READ and DIGITAL_WRITE fed mutated requests (#163), through
 * tfw_dispatch exactly as test_gpio does: every length, every bit flip of a
 * clean request, every pin against the edge values of its second byte, and
 * random bytes. Each request runs twice, from the mock's reset state and from
 * every digital pin driven high as an output, so a refused request that still
 * reached the HAL -- a mode or a write of either level -- moves a pin in one of
 * the two. The writer's buffer carries guard bytes past its capacity.
 *
 * xorshift32 with a literal seed: a failure reproduces from the iteration.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "gpio.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

#define GUARD       16u
#define RANDOM_RUNS 3000u
#define SNAP_LEN    ((unsigned)TFW_PIN_COUNT * 3u)

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD + GUARD];
static tfw_reader_t r;
static tfw_writer_t w;
static uint8_t      before[SNAP_LEN], after[SNAP_LEN];

static uint32_t rng = 0x6F10C0DEu;
static unsigned runs, refused;

static const uint8_t CMDS[] = {
    TFW_CMD_PIN_MODE, TFW_CMD_DIGITAL_READ, TFW_CMD_DIGITAL_WRITE,
};

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
    runs = refused = 0u;
}
void tearDown(void) {}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)TFW_MAX_PAYLOAD);
    return tfw_dispatch(cmd, &r, &w);
}

/* Level, direction and pull-up of every pin on the board, as the mock holds them. */
static void snapshot(uint8_t *s)
{
    tfw_pinref_t ref;
    unsigned     p;

    memset(s, 0, SNAP_LEN);
    for (p = 0u; p < TFW_PIN_COUNT; p++) {
        if (tfw_board_pin((tfw_pin_t)p, &ref) != TFW_OK) {
            continue;
        }
        s[p * 3u]      = tfw_mock_pin_level(ref.port, ref.bit);
        s[p * 3u + 1u] = tfw_mock_pin_is_output(ref.port, ref.bit);
        s[p * 3u + 2u] = tfw_mock_pin_pullup(ref.port, ref.bit);
    }
}

static void drive_all_high(void)
{
    tfw_pinref_t ref;
    unsigned     p;

    for (p = 0u; p < TFW_PIN_COUNT; p++) {
        if (tfw_board_pin((tfw_pin_t)p, &ref) == TFW_OK) {
            TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&ref, TFW_PIN_OUTPUT));
            TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&ref, 1u));
        }
    }
}

/* What each command must answer, from the commands.def arity and gpio.c's order
 * of checks. A pin past the board is BAD_PIN without asking the board. */
static tfw_status_t model(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    tfw_pinref_t ref;
    tfw_status_t st;

    if (n != (cmd == TFW_CMD_DIGITAL_READ ? 1u : 2u)) {
        return TFW_ERR_BAD_ARGC;
    }
    if (p[0] >= TFW_PIN_COUNT) {
        return TFW_ERR_BAD_PIN;
    }
    st = tfw_board_pin(p[0], &ref);
    if (st != TFW_OK || cmd == TFW_CMD_DIGITAL_READ) {
        return st;
    }
    if (tfw_board_pin_flags(p[0]) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
        return TFW_ERR_RESERVED_PIN;
    }
    if (p[1] > (cmd == TFW_CMD_PIN_MODE ? (uint8_t)TFW_PIN_INPUT_PULLUP : 1u)) {
        return TFW_ERR_RANGE;
    }
    return TFW_OK;
}

static void check(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    tfw_status_t want = model(cmd, p, n);
    unsigned     base, i;

    for (base = 0u; base < 2u; base++) {
        tfw_mock_reset();
        if (base) {
            drive_all_high();
        }
        snapshot(before);
        memset(outbuf, 0xEE, sizeof outbuf);
        TEST_ASSERT_EQUAL(want, run(cmd, p, n));
        for (i = 0u; i < GUARD; i++) {
            TEST_ASSERT_EQUAL_HEX8(0xEEu, outbuf[TFW_MAX_PAYLOAD + i]);
        }
        snapshot(after);
        if (want != TFW_OK) {
            TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
            TEST_ASSERT_EQUAL_HEX8_ARRAY(before, after, SNAP_LEN);
            refused++;
        } else {
            TEST_ASSERT_EQUAL_UINT8(cmd == TFW_CMD_DIGITAL_READ ? 1u : 0u, tfw_writer_len(&w));
            /* Only the addressed pin may move. */
            for (i = 0u; i < SNAP_LEN; i++) {
                if (i / 3u != p[0]) {
                    TEST_ASSERT_EQUAL_HEX8(before[i], after[i]);
                }
            }
        }
        runs++;
    }
}

/* D2: a pin with no other job, as test_gpio uses it. */
static void clean_request(uint8_t cmd, uint8_t *p)
{
    p[0] = 2u;
    p[1] = cmd == TFW_CMD_PIN_MODE ? (uint8_t)TFW_PIN_OUTPUT : 1u;
}

static void test_every_length_of_every_command(void)
{
    uint8_t  p[TFW_MAX_PAYLOAD];
    unsigned c, n, fill;

    for (c = 0u; c < sizeof CMDS; c++) {
        for (fill = 0u; fill < 3u; fill++) {
            for (n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
                unsigned j;

                for (j = 0u; j < sizeof p; j++) {
                    p[j] = fill == 0u ? 0x00u : fill == 1u ? 0xFFu : (uint8_t)xorshift32();
                }
                if (fill == 2u) {
                    clean_request(CMDS[c], p);
                }
                check(CMDS[c], p, (uint8_t)n);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(sizeof CMDS * 3u * (TFW_MAX_PAYLOAD + 1u) * 2u, runs);
    TEST_ASSERT_TRUE(refused > 0u);
}

static void test_every_bit_flip_of_a_clean_request(void)
{
    uint8_t  base[2], p[2];
    unsigned c, bit, n;

    for (c = 0u; c < sizeof CMDS; c++) {
        n = CMDS[c] == TFW_CMD_DIGITAL_READ ? 1u : 2u;
        clean_request(CMDS[c], base);
        check(CMDS[c], base, (uint8_t)n);
        for (bit = 0u; bit < n * 8u; bit++) {
            memcpy(p, base, sizeof p);
            p[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            check(CMDS[c], p, (uint8_t)n);
        }
    }
    TEST_ASSERT_EQUAL_UINT((3u + 16u + 8u + 16u) * 2u, runs);
}

/* Every pin, 0 through the wrap, against the second byte's edges. */
static void test_every_pin_against_every_edge(void)
{
    static const uint8_t vals[] = { 0x00u, 0x01u, 0x02u, 0x03u, 0x7Fu, 0x80u, 0xFEu, 0xFFu };
    uint8_t  p[2];
    unsigned pin, v, n = 0u;

    for (pin = 0u; pin < 256u; pin++) {
        p[0] = (uint8_t)pin;
        check(TFW_CMD_DIGITAL_READ, p, 1u);
        for (v = 0u; v < sizeof vals; v++) {
            p[1] = vals[v];
            check(TFW_CMD_PIN_MODE, p, 2u);
            check(TFW_CMD_DIGITAL_WRITE, p, 2u);
        }
        n++;
    }
    TEST_ASSERT_EQUAL_UINT(256u, n);
    TEST_ASSERT_EQUAL_UINT(256u * (1u + 2u * sizeof vals) * 2u, runs);
}

static void test_named_pin_edges(void)
{
    const uint8_t edges[] = { (uint8_t)TFW_PIN_COUNT, (uint8_t)(TFW_PIN_COUNT + 1u), 0xFFu };
    unsigned      e;

    for (e = 0u; e < sizeof edges; e++) {
        uint8_t p[2] = { edges[e], 1u };

        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, run(TFW_CMD_DIGITAL_WRITE, p, 2u));
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, run(TFW_CMD_PIN_MODE, p, 2u));
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, run(TFW_CMD_DIGITAL_READ, p, 1u));
        check(TFW_CMD_DIGITAL_WRITE, p, 2u);
    }
    TEST_ASSERT_EQUAL_UINT(sizeof edges * 2u, runs);
    TEST_ASSERT_EQUAL_UINT(runs, refused);
}

static void test_random_requests(void)
{
    uint8_t  p[4];
    unsigned i, j;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        uint32_t x   = xorshift32();
        uint8_t  cmd = CMDS[x % sizeof CMDS];
        uint8_t  n   = cmd == TFW_CMD_DIGITAL_READ ? 1u : 2u;

        for (j = 0u; j < sizeof p; j++) {
            p[j] = (uint8_t)xorshift32();
        }
        /* Mostly well-sized requests, so the checks past arity are reached. */
        if ((x >> 8) % 8u == 0u) {
            n = (uint8_t)((x >> 16) % (sizeof p + 1u));
        }
        check(cmd, p, n);
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS * 2u, runs);
}

/* After all of it, clean requests give what test_gpio pins. */
static void test_clean_requests_afterwards(void)
{
    tfw_mock_reset();
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 13u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 13u, 1u }, 2u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(TFW_ADDR_PINB, 5u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(TFW_ADDR_PIND, 5u));

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 2u, 2u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 2u));
    TEST_ASSERT_TRUE(tfw_mock_pin_pullup(TFW_ADDR_PIND, 2u));

    tfw_mock_set_input_level(TFW_ADDR_PIND, 3u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ 3u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(1u, outbuf[0]);

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 99u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 20u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 1u, 1u }, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ 1u }, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 2u, 7u }, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ 2u, 9u }, 2u));
}

/* One refusal, named: the exact status, nothing in the reply, no pin moved,
 * from a board with every pin driven high so a stray write low would show. */
static void expect_refusal(uint8_t cmd, const uint8_t *p, uint8_t n, tfw_status_t want)
{
    uint8_t  was[SNAP_LEN], now[SNAP_LEN];
    unsigned i;

    tfw_mock_reset();
    drive_all_high();
    snapshot(was);
    memset(outbuf, 0xEE, sizeof outbuf);
    TEST_ASSERT_EQUAL(want, run(cmd, p, n));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    for (i = 0u; i < sizeof outbuf; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xEEu, outbuf[i]);
    }
    snapshot(now);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(was, now, SNAP_LEN);
}

static void test_pin_mode_on_a_reserved_pin_is_refused_and_no_pin_moves(void)
{
    expect_refusal(TFW_CMD_PIN_MODE, (const uint8_t[]){ 1u, (uint8_t)TFW_PIN_INPUT }, 2u,
                   TFW_ERR_RESERVED_PIN);
}

static void test_pin_mode_on_an_unmapped_pin_is_unsupported_and_no_pin_moves(void)
{
    expect_refusal(TFW_CMD_PIN_MODE, (const uint8_t[]){ 20u, (uint8_t)TFW_PIN_OUTPUT }, 2u,
                   TFW_ERR_UNSUPPORTED);
}

static void test_pin_mode_past_the_last_pin_is_a_bad_pin_and_no_pin_moves(void)
{
    expect_refusal(TFW_CMD_PIN_MODE,
                   (const uint8_t[]){ (uint8_t)TFW_PIN_COUNT, (uint8_t)TFW_PIN_OUTPUT }, 2u,
                   TFW_ERR_BAD_PIN);
}

static void test_pin_mode_one_past_input_pullup_is_a_range_error_and_the_pin_stays(void)
{
    expect_refusal(TFW_CMD_PIN_MODE,
                   (const uint8_t[]){ 2u, (uint8_t)((uint8_t)TFW_PIN_INPUT_PULLUP + 1u) }, 2u,
                   TFW_ERR_RANGE);
}

static void test_digital_write_of_level_two_is_a_range_error_and_the_pin_stays(void)
{
    expect_refusal(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 13u, 2u }, 2u, TFW_ERR_RANGE);
}

static void test_digital_write_on_a_reserved_pin_is_refused_and_no_pin_moves(void)
{
    expect_refusal(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 1u, 0u }, 2u,
                   TFW_ERR_RESERVED_PIN);
}

static void test_digital_write_on_an_unmapped_pin_is_unsupported_and_no_pin_moves(void)
{
    expect_refusal(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ 20u, 0u }, 2u,
                   TFW_ERR_UNSUPPORTED);
}

static void test_digital_read_of_an_unmapped_pin_is_unsupported_and_replies_nothing(void)
{
    expect_refusal(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ 20u }, 1u, TFW_ERR_UNSUPPORTED);
}

static void test_digital_read_past_the_last_pin_is_a_bad_pin_and_replies_nothing(void)
{
    expect_refusal(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ (uint8_t)TFW_PIN_COUNT }, 1u,
                   TFW_ERR_BAD_PIN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_length_of_every_command);
    RUN_TEST(test_every_bit_flip_of_a_clean_request);
    RUN_TEST(test_every_pin_against_every_edge);
    RUN_TEST(test_named_pin_edges);
    RUN_TEST(test_random_requests);
    RUN_TEST(test_clean_requests_afterwards);
    RUN_TEST(test_pin_mode_on_a_reserved_pin_is_refused_and_no_pin_moves);
    RUN_TEST(test_pin_mode_on_an_unmapped_pin_is_unsupported_and_no_pin_moves);
    RUN_TEST(test_pin_mode_past_the_last_pin_is_a_bad_pin_and_no_pin_moves);
    RUN_TEST(test_pin_mode_one_past_input_pullup_is_a_range_error_and_the_pin_stays);
    RUN_TEST(test_digital_write_of_level_two_is_a_range_error_and_the_pin_stays);
    RUN_TEST(test_digital_write_on_a_reserved_pin_is_refused_and_no_pin_moves);
    RUN_TEST(test_digital_write_on_an_unmapped_pin_is_unsupported_and_no_pin_moves);
    RUN_TEST(test_digital_read_of_an_unmapped_pin_is_unsupported_and_replies_nothing);
    RUN_TEST(test_digital_read_past_the_last_pin_is_a_bad_pin_and_replies_nothing);
    return UNITY_END();
}
