/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * TIMER_SET, TIMER_CANCEL and TIMER_QUERY fed mutated requests (#163), through
 * tfw_dispatch exactly as test_sched does, against three tables: empty, D2
 * armed, and every slot armed on D2..D9 as test_sched fills it. Every length,
 * every bit flip of a clean request, every pin against the level, duration and
 * apply-final edges, and random bytes. Each request runs from the mock's reset
 * state and from every digital pin driven high first, so a refused request that
 * still reached the HAL moves a pin in one of the two; the slot count must not
 * move either. The writer's buffer carries guard bytes past its capacity.
 *
 * xorshift32 with a literal seed: a failure reproduces from the iteration.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "sched.h"
#include "gpio.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

#define PIN         2u
#define PIN_PORT    TFW_ADDR_PIND
#define PIN_BIT     2u
#define GUARD       16u
#define RANDOM_RUNS 3000u
#define SNAP_LEN    ((unsigned)TFW_PIN_COUNT * 3u)
#define TABLES      3u

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD + GUARD];
static tfw_reader_t r;
static tfw_writer_t w;
static uint8_t      before[SNAP_LEN], after[SNAP_LEN];

static uint32_t rng = 0x5C4ED00Du;
static unsigned runs, refused;

static const uint8_t CMDS[] = {
    TFW_CMD_TIMER_SET, TFW_CMD_TIMER_CANCEL, TFW_CMD_TIMER_QUERY,
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
    tfw_sched_init();
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

static tfw_status_t timer_set(tfw_pin_t pin, uint8_t level, uint32_t ms)
{
    uint8_t p[6] = { pin, level,
                     (uint8_t)(ms), (uint8_t)(ms >> 8),
                     (uint8_t)(ms >> 16), (uint8_t)(ms >> 24) };
    return run(TFW_CMD_TIMER_SET, p, 6u);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

/* Table t holds 0, 1 or TFW_SCHED_SLOTS timers, on D2 upward, slot i on pin
 * i + 2 -- the order free_slot() hands them out. */
static unsigned armed_count(unsigned t)
{
    return t == 0u ? 0u : t == 1u ? 1u : TFW_SCHED_SLOTS;
}

static void arm_table(unsigned t)
{
    unsigned i;

    for (i = 0u; i < armed_count(t); i++) {
        TEST_ASSERT_EQUAL(TFW_OK, timer_set((tfw_pin_t)(PIN + i), 1u, 5000u));
    }
    TEST_ASSERT_EQUAL_UINT8(armed_count(t), tfw_sched_active());
}

/* The slot pin is armed in, or -1. */
static int armed_slot(unsigned t, uint8_t pin)
{
    return pin >= PIN && pin < PIN + armed_count(t) ? (int)(pin - PIN) : -1;
}

/* What each command must answer, from the commands.def arity and sched.c's
 * order of checks. A pin past the board carries no flags and is BAD_PIN. */
static tfw_status_t model(unsigned t, uint8_t cmd, const uint8_t *p, uint8_t n)
{
    tfw_pinref_t ref;
    tfw_status_t st;
    uint32_t     ms;

    if (cmd == TFW_CMD_TIMER_QUERY) {
        return n == 1u ? TFW_OK : TFW_ERR_BAD_ARGC;
    }
    if (cmd == TFW_CMD_TIMER_CANCEL) {
        if (n != 2u) {
            return TFW_ERR_BAD_ARGC;
        }
        return armed_slot(t, p[0]) >= 0 ? TFW_OK : TFW_ERR_NO_SLOT;
    }
    if (n != 6u) {
        return TFW_ERR_BAD_ARGC;
    }
    if (p[0] < TFW_PIN_COUNT && (tfw_board_pin_flags(p[0]) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE))) {
        return TFW_ERR_RESERVED_PIN;
    }
    if (p[0] >= TFW_PIN_COUNT) {
        return TFW_ERR_BAD_PIN;
    }
    st = tfw_board_pin(p[0], &ref);
    if (st != TFW_OK) {
        return st;
    }
    ms = le32(&p[2]);
    if (p[1] > 1u || ms == 0u || ms > TFW_SCHED_MAX_MS) {
        return TFW_ERR_RANGE;
    }
    if (armed_slot(t, p[0]) < 0 && armed_count(t) == TFW_SCHED_SLOTS) {
        return TFW_ERR_NO_SLOT;
    }
    return TFW_OK;
}

static void check(unsigned t, uint8_t cmd, const uint8_t *p, uint8_t n)
{
    tfw_status_t want = model(t, cmd, p, n);
    unsigned     base, i, active;
    int          slot;

    for (base = 0u; base < 2u; base++) {
        tfw_mock_reset();
        tfw_sched_init();
        if (base) {
            drive_all_high();
        }
        arm_table(t);
        active = tfw_sched_active();
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
            TEST_ASSERT_EQUAL_UINT8(active, tfw_sched_active());
            refused++;
            runs++;
            continue;
        }
        /* Only the addressed pin may move, and a query moves none. */
        for (i = 0u; i < SNAP_LEN; i++) {
            if (cmd == TFW_CMD_TIMER_QUERY || i / 3u != p[0]) {
                TEST_ASSERT_EQUAL_HEX8(before[i], after[i]);
            }
        }
        slot = armed_slot(t, p[0]);
        if (cmd == TFW_CMD_TIMER_SET) {
            TEST_ASSERT_EQUAL_UINT8(1u, tfw_writer_len(&w));
            TEST_ASSERT_EQUAL_UINT8(slot >= 0 ? (unsigned)slot : armed_count(t), outbuf[0]);
            TEST_ASSERT_EQUAL_UINT8(active + (slot >= 0 ? 0u : 1u), tfw_sched_active());
            TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TIMER_QUERY, p, 1u));
            TEST_ASSERT_EQUAL_UINT8(1u, outbuf[0]);
            TEST_ASSERT_EQUAL_UINT8(p[1], outbuf[1]);
            TEST_ASSERT_EQUAL_UINT32(le32(&p[2]), le32(&outbuf[2]));
        } else if (cmd == TFW_CMD_TIMER_CANCEL) {
            TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
            TEST_ASSERT_EQUAL_UINT8(active - 1u, tfw_sched_active());
        } else {
            TEST_ASSERT_EQUAL_UINT8(6u, tfw_writer_len(&w));
            TEST_ASSERT_EQUAL_UINT8(slot >= 0 ? 1u : 0u, outbuf[0]);
            TEST_ASSERT_EQUAL_UINT32(slot >= 0 ? 5000u : 0u, le32(&outbuf[2]));
        }
        runs++;
    }
}

static uint8_t clean_request(uint8_t cmd, uint8_t *p)
{
    p[0] = PIN;
    if (cmd == TFW_CMD_TIMER_QUERY) {
        return 1u;
    }
    p[1] = 1u;
    if (cmd == TFW_CMD_TIMER_CANCEL) {
        return 2u;
    }
    p[2] = 0x88u; p[3] = 0x13u; p[4] = 0x00u; p[5] = 0x00u;   /* 5000 ms */
    return 6u;
}

static void test_every_length_of_every_command(void)
{
    uint8_t  p[TFW_MAX_PAYLOAD];
    unsigned t, c, n, fill, j;

    for (t = 0u; t < TABLES; t++) {
        for (c = 0u; c < sizeof CMDS; c++) {
            for (fill = 0u; fill < 3u; fill++) {
                for (n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
                    for (j = 0u; j < sizeof p; j++) {
                        p[j] = fill == 0u ? 0x00u : fill == 1u ? 0xFFu : (uint8_t)xorshift32();
                    }
                    if (fill == 2u) {
                        (void)clean_request(CMDS[c], p);
                    }
                    check(t, CMDS[c], p, (uint8_t)n);
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(TABLES * sizeof CMDS * 3u * (TFW_MAX_PAYLOAD + 1u) * 2u, runs);
    TEST_ASSERT_TRUE(refused > 0u);
}

static void test_every_bit_flip_of_a_clean_request(void)
{
    uint8_t  base[6], p[6];
    unsigned t, c, bit, n, total = 0u;

    for (t = 0u; t < TABLES; t++) {
        for (c = 0u; c < sizeof CMDS; c++) {
            n = clean_request(CMDS[c], base);
            check(t, CMDS[c], base, (uint8_t)n);
            total++;
            for (bit = 0u; bit < n * 8u; bit++) {
                memcpy(p, base, sizeof p);
                p[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
                check(t, CMDS[c], p, (uint8_t)n);
                total++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(TABLES * (3u + 48u + 16u + 8u), total);
    TEST_ASSERT_EQUAL_UINT(total * 2u, runs);
}

/* Every pin, 0 through the wrap, against the level, duration and apply-final
 * edges: 0, 1, the maximum, the maximum + 1 and the wrap. */
static void test_every_pin_against_every_edge(void)
{
    static const uint8_t  levels[] = { 0x00u, 0x01u, 0x02u, 0xFFu };
    static const uint32_t mss[]    = { 0u, 1u, TFW_SCHED_MAX_MS, TFW_SCHED_MAX_MS + 1u, 0xFFFFFFFFu };
    static const uint8_t  finals[] = { 0x00u, 0x01u, 0xFFu };
    uint8_t  p[6];
    unsigned t, pin, l, m, f, total = 0u;

    for (t = 0u; t < TABLES; t++) {
        for (pin = 0u; pin < 256u; pin++) {
            p[0] = (uint8_t)pin;
            check(t, TFW_CMD_TIMER_QUERY, p, 1u);
            total++;
            for (f = 0u; f < sizeof finals; f++) {
                p[1] = finals[f];
                check(t, TFW_CMD_TIMER_CANCEL, p, 2u);
                total++;
            }
            for (l = 0u; l < sizeof levels; l++) {
                for (m = 0u; m < sizeof mss / sizeof mss[0]; m++) {
                    p[1] = levels[l];
                    p[2] = (uint8_t)mss[m];
                    p[3] = (uint8_t)(mss[m] >> 8);
                    p[4] = (uint8_t)(mss[m] >> 16);
                    p[5] = (uint8_t)(mss[m] >> 24);
                    check(t, TFW_CMD_TIMER_SET, p, 6u);
                    total++;
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(TABLES * 256u * (1u + 3u + 4u * 5u), total);
    TEST_ASSERT_EQUAL_UINT(total * 2u, runs);
}

static void test_named_pin_edges(void)
{
    const uint8_t edges[] = { (uint8_t)TFW_PIN_COUNT, (uint8_t)(TFW_PIN_COUNT + 1u), 0xFFu };
    unsigned      e;

    for (e = 0u; e < sizeof edges; e++) {
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, timer_set(edges[e], 1u, 1000u));
        TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, run(TFW_CMD_TIMER_CANCEL, (const uint8_t[]){ edges[e], 1u }, 2u));
        TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TIMER_QUERY, &edges[e], 1u));
        TEST_ASSERT_EQUAL_UINT8(0u, outbuf[0]);
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sched_active());
}

static void test_random_requests(void)
{
    uint8_t  p[8];
    unsigned i, j;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        uint32_t x   = xorshift32();
        uint8_t  cmd = CMDS[x % sizeof CMDS];
        uint8_t  n   = cmd == TFW_CMD_TIMER_SET ? 6u : cmd == TFW_CMD_TIMER_CANCEL ? 2u : 1u;

        for (j = 0u; j < sizeof p; j++) {
            p[j] = (uint8_t)xorshift32();
        }
        /* Pins on the board half the time, so the checks past the pin run. */
        if ((x >> 4) & 1u) {
            p[0] = (uint8_t)(p[0] % TFW_PIN_COUNT);
        }
        if ((x >> 8) % 8u == 0u) {
            n = (uint8_t)((x >> 16) % (sizeof p + 1u));
        }
        check((x >> 24) % TABLES, cmd, p, n);
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS * 2u, runs);
}

/* After all of it, clean requests give what test_sched pins. */
static void test_clean_requests_afterwards(void)
{
    uint8_t p[2] = { PIN, 1u };
    uint8_t i;

    tfw_mock_reset();
    tfw_sched_init();
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 5000u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(PIN_PORT, PIN_BIT));
    tfw_mock_advance(4999u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(PIN_PORT, PIN_BIT));
    tfw_mock_advance(1u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(PIN_PORT, PIN_BIT));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sched_active());

    for (i = 0u; i < TFW_SCHED_SLOTS; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, timer_set((tfw_pin_t)(i + 2u), 1u, 1000u));
    }
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, timer_set(12u, 1u, 1000u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TIMER_CANCEL, p, 2u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(PIN_PORT, PIN_BIT));
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, run(TFW_CMD_TIMER_CANCEL, p, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, timer_set(PIN, 1u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, timer_set(PIN, 1u, 0x80000000u));
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 0x7FFFFFFFu));
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, timer_set(1u, 1u, 1000u));
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
    return UNITY_END();
}
