/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The template emitter fed mutated step tables, source bytes and windows
 * (#163), walked exactly as test_tmpl walks it. Sixteen sources of 512 bytes
 * keep every step in bounds whatever its offset and length, so every byte of a
 * step -- kind, source index, offset, length -- is fair game: every bit flip of
 * test_tmpl's table, every truncation of it, all 0x00, all 0xFF and random
 * tables. Each document is checked against a reference built here by hand from
 * the step bytes: the hash covers all of it, the window holds exactly
 * min(cap, len - from) bytes of it, and nothing lands past the capacity.
 *
 * xorshift32 with a literal seed: a failure reproduces from the iteration.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tmpl.h"
#include "tfw/progmem.h"

#define GUARD       16u
#define RANDOM_RUNS 2000u
#define SOURCES     16u
#define SRC_LEN     512u
#define MAX_STEPS   8u
#define DOC_MAX     (MAX_STEPS * 255u * 2u)
#define CAP_MAX     255u

static uint8_t               SRCB[SOURCES][SRC_LEN];
static const uint8_t        *SRC[SOURCES];
static tfw_tmpl_step_t       steps[MAX_STEPS];
static uint8_t               doc[DOC_MAX];
static uint8_t               outbuf[CAP_MAX + GUARD];

static uint32_t rng = 0x7E3D10C5u;
static unsigned runs;

/* test_tmpl's table, its kinds over sources 0..3. */
static const tfw_tmpl_step_t CLEAN[] = {
    { TFW_TMPL_PGM | 0u, 0u, 2u },
    { TFW_TMPL_RAM | 1u, 1u, 2u },
    { TFW_TMPL_HEX | 2u, 0u, 4u },
    { TFW_TMPL_POS | 3u, 0u, 4u },
    { TFW_TMPL_PGM | 0u, 2u, 3u },
    { TFW_TMPL_RAM | 1u, 0u, 0u },
};
#define CLEAN_COUNT ((uint8_t)(sizeof CLEAN / sizeof CLEAN[0]))

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void fill_sources(unsigned how)
{
    unsigned s, i;

    for (s = 0u; s < SOURCES; s++) {
        for (i = 0u; i < SRC_LEN; i++) {
            SRCB[s][i] = how == 0u ? 0x00u : how == 1u ? 0xFFu : (uint8_t)xorshift32();
        }
        SRC[s] = SRCB[s];
    }
}

void setUp(void)
{
    fill_sources(2u);
    runs = 0u;
}
void tearDown(void) {}

static char hex(uint8_t v)
{
    return "0123456789abcdef"[v & 0x0Fu];
}

/* The document, from the step bytes alone. */
static uint16_t reference(uint8_t count)
{
    uint16_t len = 0u;
    unsigned k, i;

    for (k = 0u; k < count; k++) {
        uint8_t        kind = (uint8_t)(steps[k].src & 0xF0u);
        const uint8_t *p    = SRCB[steps[k].src & 0x0Fu] + steps[k].off;

        for (i = 0u; i < steps[k].n; i++) {
            if (kind == 0x20u) {
                doc[len++] = (uint8_t)hex((uint8_t)(p[i] >> 4));
                doc[len++] = (uint8_t)hex(p[i]);
            } else if (kind == 0x30u) {
                doc[len++] = (uint8_t)((p[i] & 0x3Fu) | 0x40u);
            } else {
                doc[len++] = p[i];
            }
        }
    }
    return len;
}

static void sha(const uint8_t *p, uint16_t n, uint8_t out[TFW_SHA256_LEN])
{
    tfw_sha256_t c;

    tfw_sha256_init(&c);
    tfw_sha256_update(&c, p, n);
    tfw_sha256_final(&c, out);
}

static void window(uint8_t count, uint16_t len, const uint8_t *want_hash, uint16_t from, uint8_t cap)
{
    tfw_emit_t   e;
    tfw_sha256_t h;
    uint8_t      got_hash[TFW_SHA256_LEN];
    unsigned     want, i;

    want = from >= len ? 0u : (unsigned)(len - from) < cap ? (unsigned)(len - from) : cap;
    memset(outbuf, 0xEE, sizeof outbuf);
    memset(&e, 0, sizeof e);
    tfw_sha256_init(&h);
    e.hash = &h;
    e.out  = outbuf;
    e.from = from;
    e.cap  = cap;
    tfw_tmpl_emit(&e, steps, count, SRC);
    tfw_sha256_final(&h, got_hash);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_hash, got_hash, TFW_SHA256_LEN);
    TEST_ASSERT_EQUAL_UINT16(len, e.pos);
    TEST_ASSERT_EQUAL_UINT8(want, e.got);
    if (want) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&doc[from], outbuf, want);
    }
    for (i = want; i < sizeof outbuf; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xEEu, outbuf[i]);
    }
    runs++;
}

/* One table: the digest, a hash-only walk, a window-only walk, and the window
 * edges from in {0, 1, len - 1, len, len + 1, 0xFFFF} against cap in
 * {0, 1, len, 255}, plus one random window. */
static void check(uint8_t count)
{
    uint8_t  want_hash[TFW_SHA256_LEN], got[TFW_SHA256_LEN];
    uint16_t len = reference(count);
    uint16_t froms[6];
    uint8_t  caps[4];
    unsigned f, c;
    tfw_emit_t e;

    sha(doc, len, want_hash);
    tfw_tmpl_digest(steps, count, SRC, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_hash, got, TFW_SHA256_LEN);

    memset(&e, 0, sizeof e);
    memset(outbuf, 0xEE, sizeof outbuf);
    e.cap = CAP_MAX;
    tfw_tmpl_emit(&e, steps, count, SRC);      /* no hash, no out */
    TEST_ASSERT_EQUAL_UINT16(len, e.pos);
    TEST_ASSERT_EQUAL_UINT8(0u, e.got);

    froms[0] = 0u;
    froms[1] = 1u;
    froms[2] = (uint16_t)(len - 1u);
    froms[3] = len;
    froms[4] = (uint16_t)(len + 1u);
    froms[5] = 0xFFFFu;
    caps[0]  = 0u;
    caps[1]  = 1u;
    caps[2]  = len > CAP_MAX ? (uint8_t)CAP_MAX : (uint8_t)len;
    caps[3]  = (uint8_t)CAP_MAX;
    for (f = 0u; f < sizeof froms / sizeof froms[0]; f++) {
        for (c = 0u; c < sizeof caps; c++) {
            window(count, len, want_hash, froms[f], caps[c]);
        }
    }
    window(count, len, want_hash, (uint16_t)(xorshift32() % (len + 2u)), (uint8_t)xorshift32());
}

#define PER_CHECK 25u

static void test_every_bit_flip_of_the_clean_table(void)
{
    unsigned bit, total = 0u;

    memcpy(steps, CLEAN, sizeof CLEAN);
    check(CLEAN_COUNT);
    total++;
    for (bit = 0u; bit < sizeof CLEAN * 8u; bit++) {
        memcpy(steps, CLEAN, sizeof CLEAN);
        ((uint8_t *)steps)[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        check(CLEAN_COUNT);
        total++;
    }
    TEST_ASSERT_EQUAL_UINT(1u + sizeof CLEAN * 8u, total);
    TEST_ASSERT_EQUAL_UINT(total * PER_CHECK, runs);
}

static void test_every_truncation_of_the_clean_table(void)
{
    unsigned n;

    memcpy(steps, CLEAN, sizeof CLEAN);
    for (n = 0u; n <= CLEAN_COUNT; n++) {
        check((uint8_t)n);
    }
    TEST_ASSERT_EQUAL_UINT((CLEAN_COUNT + 1u) * PER_CHECK, runs);
}

/* Tables of all 0x00 and all 0xFF bytes, over sources of all 0x00, all 0xFF
 * and random bytes, at every count up to MAX_STEPS. */
static void test_all_zero_and_all_ones_tables(void)
{
    unsigned fill, how, n;

    for (fill = 0u; fill < 2u; fill++) {
        for (how = 0u; how < 3u; how++) {
            fill_sources(how);
            memset(steps, fill ? 0xFF : 0x00, sizeof steps);
            for (n = 0u; n <= MAX_STEPS; n++) {
                check((uint8_t)n);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 3u * (MAX_STEPS + 1u) * PER_CHECK, runs);
}

/* Each step field at 0, 1, the maximum, the maximum + 1 and the wrap: the
 * source index 0, 1, 15, 16 (which is the next kind's source 0) and 0xFF; the
 * offset and length 0, 1, 254, 255 and the wrap to 0. */
static void test_every_field_edge_of_one_step(void)
{
    static const uint8_t kinds[] = { 0x00u, 0x10u, 0x20u, 0x30u, 0x40u, 0xF0u };
    static const uint8_t srcs[]  = { 0u, 1u, 15u, 16u, 0xFFu };
    static const uint8_t vals[]  = { 0u, 1u, 254u, 255u, (uint8_t)256u };
    unsigned k, s, o, n;

    for (k = 0u; k < sizeof kinds; k++) {
        for (s = 0u; s < sizeof srcs; s++) {
            for (o = 0u; o < sizeof vals; o++) {
                for (n = 0u; n < sizeof vals; n++) {
                    steps[0].src = (uint8_t)(kinds[k] | srcs[s]);
                    steps[0].off = vals[o];
                    steps[0].n   = vals[n];
                    check(1u);
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(sizeof kinds * sizeof srcs * sizeof vals * sizeof vals * PER_CHECK, runs);
}

static void test_random_tables(void)
{
    unsigned i, j;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        uint32_t x = xorshift32();

        for (j = 0u; j < sizeof steps; j++) {
            ((uint8_t *)steps)[j] = (uint8_t)xorshift32();
        }
        /* A source byte flipped now and then, so the same table reads new data. */
        if ((x & 7u) == 0u) {
            SRCB[(x >> 4) % SOURCES][(x >> 8) % SRC_LEN] ^= (uint8_t)(1u << ((x >> 20) % 8u));
        }
        check((uint8_t)((x >> 24) % (MAX_STEPS + 1u)));
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS * PER_CHECK, runs);
}

/* After all of it, test_tmpl's table over its sources gives its document. */
static void test_clean_document_afterwards(void)
{
    static const uint8_t FIX[] TFW_PROGMEM = { 0x30u, 0x82u, 0xAAu, 0x01u, 0x02u };
    static const uint8_t RAW[]  = { 0x10u, 0x20u, 0x30u, 0x40u };
    static const uint8_t HEXS[] = { 0x00u, 0x9Fu, 0xA5u, 0xFFu };
    static const uint8_t POSS[] = { 0x00u, 0xFFu, 0x80u, 0x12u };
    static const uint8_t *const SRC4[] = { FIX, RAW, HEXS, POSS };
    static const uint8_t DOC[] = {
        0x30u, 0x82u, 0x20u, 0x30u, '0', '0', '9', 'f', 'a', '5', 'f', 'f',
        0x40u, 0x7Fu, 0x40u, 0x52u, 0xAAu, 0x01u, 0x02u,
    };
    uint8_t      want[TFW_SHA256_LEN], got[TFW_SHA256_LEN];
    tfw_emit_t   e;
    tfw_sha256_t h;

    memset(outbuf, 0xEE, sizeof outbuf);
    memset(&e, 0, sizeof e);
    tfw_sha256_init(&h);
    e.hash = &h;
    e.out  = outbuf;
    e.cap  = CAP_MAX;
    tfw_tmpl_emit(&e, CLEAN, CLEAN_COUNT, SRC4);
    tfw_sha256_final(&h, got);
    TEST_ASSERT_EQUAL_UINT8(sizeof DOC, e.got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DOC, outbuf, sizeof DOC);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, outbuf[sizeof DOC]);
    sha(DOC, (uint16_t)sizeof DOC, want);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, TFW_SHA256_LEN);
    tfw_tmpl_digest(CLEAN, CLEAN_COUNT, SRC4, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, TFW_SHA256_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_bit_flip_of_the_clean_table);
    RUN_TEST(test_every_truncation_of_the_clean_table);
    RUN_TEST(test_all_zero_and_all_ones_tables);
    RUN_TEST(test_every_field_edge_of_one_step);
    RUN_TEST(test_random_tables);
    RUN_TEST(test_clean_document_afterwards);
    return UNITY_END();
}
