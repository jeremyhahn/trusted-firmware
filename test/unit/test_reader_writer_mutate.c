/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded random sequences of every read and write primitive
 * (src/core/reader.c, src/core/writer.c) against buffers of capacity 0 to 64,
 * each run compared step by step with a plain model kept here.
 *
 * The model is the contract in reader.h and writer.h: a call that fits moves
 * the cursor by its width and reads or stores little-endian; one that does
 * not sets the sticky error, moves nothing and stores nothing; and every call
 * after that fails the same way, whatever room is left. The buffer sits
 * between guard bytes that a read must never return and a write must never
 * touch, and every reader run is repeated on an exact-size heap copy so an
 * over-read is the sanitizer's too.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "reader.h"
#include "writer.h"

#define READER_RUNS 4000u
#define WRITER_RUNS 4000u
#define MAX_OPS     24u
#define MAX_CAP     64u
#define MAX_BLOB    70u           /* past every capacity, so a blob can overrun */
#define GUARD       16u
#define GUARD_BYTE  0xC3u

static uint32_t rng = 0x3C0DE5EEu;
static unsigned runs, ops, refused;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void) { runs = ops = refused = 0u; }
void tearDown(void) { }

/* The buffer between its guards. */
static uint8_t area[GUARD + MAX_CAP + GUARD];

static void fill_area(uint8_t cap)
{
    unsigned i;

    memset(area, GUARD_BYTE, sizeof area);
    for (i = 0u; i < cap; i++) {
        area[GUARD + i] = (uint8_t)xorshift32();
    }
}

static void check_guards(uint8_t cap)
{
    unsigned i;

    for (i = 0u; i < GUARD; i++) {
        TEST_ASSERT_EQUAL_HEX8(GUARD_BYTE, area[i]);
    }
    for (i = GUARD + cap; i < sizeof area; i++) {
        TEST_ASSERT_EQUAL_HEX8(GUARD_BYTE, area[i]);
    }
}

/* ------------------------------------------------------------------ reader */

typedef struct {
    const uint8_t *buf;
    uint8_t        len, pos, fail;
} rmodel_t;

/* What a read of width n gives under the model: 1 and the bytes' offset, or 0. */
static int rmodel_take(rmodel_t *m, uint8_t n, uint8_t *at)
{
    if (m->fail) {
        return 0;
    }
    if (n > m->len - m->pos) {
        m->fail = 1u;
        return 0;
    }
    *at    = m->pos;
    m->pos = (uint8_t)(m->pos + n);
    return 1;
}

static uint32_t le(const uint8_t *p, uint8_t n)
{
    uint32_t v = 0u;

    while (n--) {
        v = (v << 8) | p[n];
    }
    return v;
}

static void check_reader(const tfw_reader_t *r, const rmodel_t *m)
{
    TEST_ASSERT_EQUAL_UINT8(m->fail ? 0u : 1u, tfw_reader_ok(r));
    TEST_ASSERT_EQUAL_UINT8(m->pos, r->pos);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(m->len - m->pos), tfw_reader_remaining(r));
}

/* One step: a primitive picked by `pick`, a blob width by `w`. */
static void read_step(tfw_reader_t *r, rmodel_t *m, uint32_t pick, uint8_t w)
{
    uint8_t at = 0u;

    switch (pick % 4u) {
    case 0u: {
        uint8_t v = tfw_read_u8(r);
        TEST_ASSERT_EQUAL_HEX8(rmodel_take(m, 1u, &at) ? m->buf[at] : 0u, v);
        break;
    }
    case 1u: {
        uint16_t v = tfw_read_u16(r);
        TEST_ASSERT_EQUAL_HEX16(rmodel_take(m, 2u, &at) ? (uint16_t)le(&m->buf[at], 2u) : 0u, v);
        break;
    }
    case 2u: {
        uint32_t v = tfw_read_u32(r);
        TEST_ASSERT_EQUAL_HEX32(rmodel_take(m, 4u, &at) ? le(&m->buf[at], 4u) : 0u, v);
        break;
    }
    default: {
        const uint8_t *p = tfw_read_blob(r, w);
        if (rmodel_take(m, w, &at)) {
            TEST_ASSERT_TRUE(p == &m->buf[at]);
        } else {
            TEST_ASSERT_NULL(p);
        }
        break;
    }
    }
    check_reader(r, m);
}

static void test_random_read_sequences_match_the_model(void)
{
    uint32_t pick[MAX_OPS];
    uint8_t  w[MAX_OPS];
    unsigned i, k, n;

    for (i = 0u; i < READER_RUNS; i++) {
        uint8_t      cap = (uint8_t)(xorshift32() % (MAX_CAP + 1u));
        uint8_t     *exact;
        tfw_reader_t r;
        rmodel_t     m;

        n = 1u + xorshift32() % MAX_OPS;
        for (k = 0u; k < n; k++) {
            pick[k] = xorshift32();
            w[k]    = (uint8_t)(xorshift32() % (MAX_BLOB + 1u));
        }

        /* Between the guards. */
        fill_area(cap);
        tfw_reader_init(&r, &area[GUARD], cap);
        m.buf = &area[GUARD]; m.len = cap; m.pos = 0u; m.fail = 0u;
        check_reader(&r, &m);
        for (k = 0u; k < n; k++) {
            read_step(&r, &m, pick[k], w[k]);
            ops++;
        }
        check_guards(cap);
        refused += m.fail;

        /* The same steps on an exact-size heap copy. */
        exact = malloc(cap ? cap : 1u);
        TEST_ASSERT_NOT_NULL(exact);
        memcpy(exact, &area[GUARD], cap);
        tfw_reader_init(&r, exact, cap);
        m.buf = exact; m.len = cap; m.pos = 0u; m.fail = 0u;
        for (k = 0u; k < n; k++) {
            read_step(&r, &m, pick[k], w[k]);
        }
        free(exact);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(READER_RUNS, runs);
    TEST_ASSERT_NOT_EQUAL(0u, refused);
    TEST_ASSERT_NOT_EQUAL(READER_RUNS, refused);
}

/* Every capacity, every width: reads until the first refusal, then every
 * later read of any width is refused too, with room left or not. */
static void test_a_short_read_stays_failed(void)
{
    static const uint8_t widths[] = { 1u, 2u, 4u };
    unsigned cap, k, got;

    for (cap = 0u; cap <= MAX_CAP; cap++) {
        for (k = 0u; k < sizeof widths; k++) {
            uint8_t      wd = widths[k];
            tfw_reader_t r;
            rmodel_t     m;

            fill_area((uint8_t)cap);
            tfw_reader_init(&r, &area[GUARD], (uint8_t)cap);
            m.buf = &area[GUARD]; m.len = (uint8_t)cap; m.pos = 0u; m.fail = 0u;
            got = 0u;
            while (tfw_reader_ok(&r)) {
                read_step(&r, &m, wd == 1u ? 0u : (wd == 2u ? 1u : 2u), 0u);
                got += tfw_reader_ok(&r);
            }
            TEST_ASSERT_EQUAL_UINT(cap / wd, got);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(got * wd), r.pos);
            read_step(&r, &m, 0u, 0u);
            read_step(&r, &m, 3u, 0u);    /* even a zero-length blob */
            TEST_ASSERT_FALSE(tfw_reader_ok(&r));
            check_guards((uint8_t)cap);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT((MAX_CAP + 1u) * 3u, runs);
}

/* ------------------------------------------------------------------ writer */

typedef struct {
    uint8_t cap, pos, fail;
    uint8_t buf[MAX_CAP];
} wmodel_t;

static void wmodel_put(wmodel_t *m, const uint8_t *b, uint8_t n)
{
    if (m->fail) {
        return;
    }
    if (n > m->cap - m->pos) {
        m->fail = 1u;
        return;
    }
    memcpy(&m->buf[m->pos], b, n);
    m->pos = (uint8_t)(m->pos + n);
}

static void check_writer(const tfw_writer_t *w, const wmodel_t *m)
{
    TEST_ASSERT_EQUAL_UINT8(m->fail ? 0u : 1u, tfw_writer_ok(w));
    TEST_ASSERT_EQUAL_UINT8(m->pos, tfw_writer_len(w));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(m->cap - m->pos), tfw_writer_remaining(w));
    if (m->cap) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(m->buf, &area[GUARD], m->cap);
    }
    check_guards(m->cap);
}

static void write_step(tfw_writer_t *w, wmodel_t *m, uint32_t pick, uint8_t n)
{
    uint8_t  src[MAX_BLOB];
    uint32_t v = xorshift32();
    uint8_t  b[4];
    unsigned i;

    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
    switch (pick % 4u) {
    case 0u: tfw_write_u8(w, (uint8_t)v);   wmodel_put(m, b, 1u); break;
    case 1u: tfw_write_u16(w, (uint16_t)v); wmodel_put(m, b, 2u); break;
    case 2u: tfw_write_u32(w, v);           wmodel_put(m, b, 4u); break;
    default:
        for (i = 0u; i < n; i++) {
            src[i] = (uint8_t)xorshift32();
        }
        tfw_write_blob(w, src, n);
        wmodel_put(m, src, n);
        break;
    }
    check_writer(w, m);
}

static void writer_start(tfw_writer_t *w, wmodel_t *m, uint8_t cap)
{
    fill_area(cap);
    tfw_writer_init(w, &area[GUARD], cap);
    m->cap = cap; m->pos = 0u; m->fail = 0u;
    memcpy(m->buf, &area[GUARD], cap);
    check_writer(w, m);
}

static void test_random_write_sequences_match_the_model(void)
{
    unsigned i, k, n;

    for (i = 0u; i < WRITER_RUNS; i++) {
        tfw_writer_t w;
        wmodel_t     m;

        writer_start(&w, &m, (uint8_t)(xorshift32() % (MAX_CAP + 1u)));
        n = 1u + xorshift32() % MAX_OPS;
        for (k = 0u; k < n; k++) {
            uint32_t pick = xorshift32();
            write_step(&w, &m, pick, (uint8_t)(xorshift32() % (MAX_BLOB + 1u)));
            ops++;
        }
        refused += m.fail;
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(WRITER_RUNS, runs);
    TEST_ASSERT_NOT_EQUAL(0u, refused);
    TEST_ASSERT_NOT_EQUAL(WRITER_RUNS, refused);
}

/* Every capacity, every width: writes until the first refusal, which stores
 * nothing, then every later write of any width is refused too. */
static void test_a_short_write_stays_failed(void)
{
    static const uint8_t widths[] = { 1u, 2u, 4u };
    unsigned cap, k, got;

    for (cap = 0u; cap <= MAX_CAP; cap++) {
        for (k = 0u; k < sizeof widths; k++) {
            uint8_t      wd = widths[k];
            tfw_writer_t w;
            wmodel_t     m;

            writer_start(&w, &m, (uint8_t)cap);
            got = 0u;
            while (tfw_writer_ok(&w)) {
                write_step(&w, &m, wd == 1u ? 0u : (wd == 2u ? 1u : 2u), 0u);
                got += tfw_writer_ok(&w);
            }
            TEST_ASSERT_EQUAL_UINT(cap / wd, got);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(got * wd), tfw_writer_len(&w));
            write_step(&w, &m, 0u, 0u);
            write_step(&w, &m, 3u, 0u);   /* even a zero-length blob */
            TEST_ASSERT_FALSE(tfw_writer_ok(&w));
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT((MAX_CAP + 1u) * 3u, runs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_random_read_sequences_match_the_model);
    RUN_TEST(test_a_short_read_stays_failed);
    RUN_TEST(test_random_write_sequences_match_the_model);
    RUN_TEST(test_a_short_write_stays_failed);
    return UNITY_END();
}
