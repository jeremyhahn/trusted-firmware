/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The template walker (src/caps/tmpl.c), on its own. test_alias and test_csr
 * pin it through whole signed documents; this suite pins the window
 * arithmetic (from, cap, got, pos), the hash-only mode and every step kind
 * against small hand-built tables whose bytes are written out below.
 */
#include <string.h>
#include "unity.h"
#include "tmpl.h"
#include "tfw/progmem.h"

void setUp(void) {}
void tearDown(void) {}

/* Source 0: flash. */
static const uint8_t FIX[] TFW_PROGMEM = { 0x30u, 0x82u, 0xAAu, 0x01u, 0x02u };
/* Source 1: RAM, verbatim. */
static const uint8_t RAW[] = { 0x10u, 0x20u, 0x30u, 0x40u };
/* Source 2: RAM, as hex digits. */
static const uint8_t HEXS[] = { 0x00u, 0x9Fu, 0xA5u, 0xFFu };
/* Source 3: RAM, as a positive INTEGER's leading byte. */
static const uint8_t POSS[] = { 0x00u, 0xFFu, 0x80u, 0x12u };

static const uint8_t *const SRC[] = { FIX, RAW, HEXS, POSS };

static const tfw_tmpl_step_t STEPS[] TFW_PROGMEM = {
    { TFW_TMPL_PGM | 0u, 0u, 2u },
    { TFW_TMPL_RAM | 1u, 1u, 2u },
    { TFW_TMPL_HEX | 2u, 0u, 4u },
    { TFW_TMPL_POS | 3u, 0u, 4u },
    { TFW_TMPL_PGM | 0u, 2u, 3u },
    { TFW_TMPL_RAM | 1u, 0u, 0u },   /* a zero-length step emits nothing */
};
#define STEP_COUNT ((uint8_t)(sizeof STEPS / sizeof STEPS[0]))

/* The document the table above describes, by hand. */
static const uint8_t DOC[] = {
    0x30u, 0x82u,                                     /* PGM 0[0..2) */
    0x20u, 0x30u,                                     /* RAM 1[1..3) */
    '0', '0', '9', 'f', 'a', '5', 'f', 'f',           /* HEX 2[0..4) */
    0x40u, 0x7Fu, 0x40u, 0x52u,                       /* POS 3[0..4) */
    0xAAu, 0x01u, 0x02u,                              /* PGM 0[2..5) */
};
#define DOC_LEN ((uint16_t)sizeof DOC)

static void walk(tfw_emit_t *e, tfw_sha256_t *h, uint8_t *out,
                 uint16_t from, uint8_t cap)
{
    memset(e, 0, sizeof *e);
    e->hash = h;
    e->out  = out;
    e->from = from;
    e->cap  = cap;
    tfw_tmpl_emit(e, STEPS, STEP_COUNT, SRC);
}

static void sha(const uint8_t *p, uint16_t n, uint8_t out[TFW_SHA256_LEN])
{
    tfw_sha256_t c;

    tfw_sha256_init(&c);
    tfw_sha256_update(&c, p, n);
    tfw_sha256_final(&c, out);
}

static void test_whole_document_into_a_large_buffer(void)
{
    uint8_t    buf[64];
    tfw_emit_t e;

    memset(buf, 0xEE, sizeof buf);
    walk(&e, NULL, buf, 0u, 255u);
    TEST_ASSERT_EQUAL_UINT16(DOC_LEN, e.pos);
    TEST_ASSERT_EQUAL_UINT8(DOC_LEN, e.got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DOC, buf, DOC_LEN);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[DOC_LEN]);
}

/* One window, checked against [from, from+cap) of DOC. */
static void check_window(uint16_t from, uint8_t cap)
{
    uint8_t    buf[48];
    uint8_t    want;
    tfw_emit_t e;

    want = from >= DOC_LEN ? 0u
         : (uint8_t)(cap < DOC_LEN - from ? cap : DOC_LEN - from);
    memset(buf, 0xEE, sizeof buf);
    walk(&e, NULL, buf, from, cap);
    TEST_ASSERT_EQUAL_UINT16(DOC_LEN, e.pos);
    TEST_ASSERT_EQUAL_UINT8(want, e.got);
    TEST_ASSERT_EQUAL_INT(0, memcmp(buf, DOC + (from < DOC_LEN ? from : 0u), want));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[want]);
}

static void test_window_named_edges(void)
{
    check_window(0u, 5u);                       /* from the first byte */
    check_window(0u, (uint8_t)DOC_LEN);         /* exactly the document */
    check_window(7u, 6u);                       /* across the HEX/POS seam */
    check_window(DOC_LEN - 1u, 1u);             /* the last byte */
    check_window(DOC_LEN - 1u, 8u);             /* the last byte, cap to spare */
    check_window(DOC_LEN, 8u);                  /* just past the end: got 0 */
    check_window(300u, 8u);                     /* far past the end: got 0 */
    check_window(0u, 0u);                       /* cap 0 */
    check_window(9u, 0u);                       /* cap 0 mid-document */
}

static void test_every_window_is_a_slice_of_the_whole(void)
{
    uint16_t k;
    uint8_t  n;

    for (k = 0u; k <= DOC_LEN + 2u; k++) {
        for (n = 0u; n <= DOC_LEN + 2u; n++) {
            check_window(k, n);
        }
    }
}

static void test_hash_only_is_sha256_of_the_whole(void)
{
    uint8_t      want[TFW_SHA256_LEN];
    uint8_t      got[TFW_SHA256_LEN];
    tfw_sha256_t h;
    tfw_emit_t   e;

    sha(DOC, DOC_LEN, want);
    tfw_sha256_init(&h);
    /* from and cap are ignored without an out buffer. */
    walk(&e, &h, NULL, 3u, 4u);
    tfw_sha256_final(&h, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, TFW_SHA256_LEN);
    TEST_ASSERT_EQUAL_UINT16(DOC_LEN, e.pos);
    TEST_ASSERT_EQUAL_UINT8(0u, e.got);
}

/* Both sinks at once: the hash still covers every byte, the window only its own. */
static void test_hash_and_window_together(void)
{
    uint8_t      want[TFW_SHA256_LEN];
    uint8_t      got[TFW_SHA256_LEN];
    uint8_t      buf[8];
    tfw_sha256_t h;
    tfw_emit_t   e;

    sha(DOC, DOC_LEN, want);
    tfw_sha256_init(&h);
    walk(&e, &h, buf, 10u, 4u);
    tfw_sha256_final(&h, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, TFW_SHA256_LEN);
    TEST_ASSERT_EQUAL_UINT8(4u, e.got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DOC + 10, buf, 4u);
}

static void test_digest_equals_the_hash_only_walk(void)
{
    uint8_t      walked[TFW_SHA256_LEN];
    uint8_t      digest[TFW_SHA256_LEN];
    tfw_sha256_t h;
    tfw_emit_t   e;

    tfw_sha256_init(&h);
    walk(&e, &h, NULL, 0u, 0u);
    tfw_sha256_final(&h, walked);
    tfw_tmpl_digest(STEPS, STEP_COUNT, SRC, digest);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(walked, digest, TFW_SHA256_LEN);
}

static void test_empty_table_emits_nothing(void)
{
    uint8_t      want[TFW_SHA256_LEN];
    uint8_t      digest[TFW_SHA256_LEN];
    uint8_t      buf[4] = { 0xEEu, 0xEEu, 0xEEu, 0xEEu };
    tfw_emit_t   e;

    memset(&e, 0, sizeof e);
    e.out = buf;
    e.cap = 4u;
    tfw_tmpl_emit(&e, STEPS, 0u, SRC);
    TEST_ASSERT_EQUAL_UINT16(0u, e.pos);
    TEST_ASSERT_EQUAL_UINT8(0u, e.got);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[0]);
    sha(NULL, 0u, want);
    tfw_tmpl_digest(STEPS, 0u, SRC, digest);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, digest, TFW_SHA256_LEN);
}

/* One table of one step, emitted whole. */
static uint8_t emit_one(const tfw_tmpl_step_t *step, const uint8_t *const *src,
                        uint8_t *out, uint8_t cap)
{
    tfw_emit_t e;

    memset(&e, 0, sizeof e);
    e.out = out;
    e.cap = cap;
    tfw_tmpl_emit(&e, step, 1u, src);
    TEST_ASSERT_EQUAL_UINT16(e.got, e.pos);
    return e.got;
}

static const uint8_t KIND_PGM[] TFW_PROGMEM = { 0x00u, 0x7Fu, 0x80u, 0xFFu };
static const uint8_t KIND_RAM[] = { 0xFFu, 0x80u, 0x7Fu, 0x00u };
/* Every nibble value 0..f in both halves. */
static const uint8_t KIND_HEX[] = { 0x01u, 0x23u, 0x45u, 0x67u,
                                    0x89u, 0xABu, 0xCDu, 0xEFu };
/* Clear, 0x40 set, top bit set, both set, low bits kept. */
static const uint8_t KIND_POS[] = { 0x00u, 0x40u, 0x80u, 0xC0u,
                                    0x3Fu, 0xFFu, 0x81u, 0x12u };

static const uint8_t *const KIND_SRC[] = { KIND_PGM, KIND_RAM, KIND_HEX, KIND_POS };

static const tfw_tmpl_step_t STEP_PGM[] TFW_PROGMEM = { { TFW_TMPL_PGM | 0u, 1u, 3u } };
static const tfw_tmpl_step_t STEP_RAM[] TFW_PROGMEM = { { TFW_TMPL_RAM | 1u, 0u, 4u } };
static const tfw_tmpl_step_t STEP_HEX[] TFW_PROGMEM = { { TFW_TMPL_HEX | 2u, 0u, 8u } };
static const tfw_tmpl_step_t STEP_POS[] TFW_PROGMEM = { { TFW_TMPL_POS | 3u, 0u, 8u } };

static void test_kind_pgm_is_verbatim_from_offset(void)
{
    static const uint8_t want[] = { 0x7Fu, 0x80u, 0xFFu };
    uint8_t buf[8];

    TEST_ASSERT_EQUAL_UINT8(3u, emit_one(STEP_PGM, KIND_SRC, buf, 8u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, 3u);
}

static void test_kind_ram_is_verbatim(void)
{
    uint8_t buf[8];

    TEST_ASSERT_EQUAL_UINT8(4u, emit_one(STEP_RAM, KIND_SRC, buf, 8u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(KIND_RAM, buf, 4u);
}

static void test_kind_hex_is_two_lowercase_digits_per_byte(void)
{
    static const char want[] = "0123456789abcdef";
    uint8_t buf[20];

    TEST_ASSERT_EQUAL_UINT8(16u, emit_one(STEP_HEX, KIND_SRC, buf, 20u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)want, buf, 16u);
}

/* A window that splits a byte's two hex digits takes only the one it covers. */
static void test_kind_hex_window_splits_a_byte(void)
{
    uint8_t    buf[4];
    tfw_emit_t e;

    memset(&e, 0, sizeof e);
    e.out  = buf;
    e.from = 3u;
    e.cap  = 2u;
    tfw_tmpl_emit(&e, STEP_HEX, 1u, KIND_SRC);
    TEST_ASSERT_EQUAL_UINT8(2u, e.got);
    TEST_ASSERT_EQUAL_HEX8('3', buf[0]);
    TEST_ASSERT_EQUAL_HEX8('4', buf[1]);
    TEST_ASSERT_EQUAL_UINT16(16u, e.pos);
}

static void test_kind_pos_sets_0x40_and_clears_the_top_bit(void)
{
    static const uint8_t want[] = { 0x40u, 0x40u, 0x40u, 0x40u,
                                    0x7Fu, 0x7Fu, 0x41u, 0x52u };
    uint8_t buf[8];

    TEST_ASSERT_EQUAL_UINT8(8u, emit_one(STEP_POS, KIND_SRC, buf, 8u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, 8u);
}

/* The low nibble of src names the source: index 15 is the last one it can. */
static void test_source_index_fifteen(void)
{
    static const uint8_t last[] = { 0x5Au, 0xA5u };
    static const tfw_tmpl_step_t step[] TFW_PROGMEM = { { TFW_TMPL_RAM | 15u, 0u, 2u } };
    const uint8_t *src[16];
    uint8_t        buf[4];
    uint8_t        i;

    for (i = 0u; i < 16u; i++) {
        src[i] = KIND_RAM;
    }
    src[15] = last;
    TEST_ASSERT_EQUAL_UINT8(2u, emit_one(step, src, buf, 4u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(last, buf, 2u);
}

/* pos and from are 16-bit: a document past 255 bytes windows correctly. */
static uint8_t BIG[200];
static const tfw_tmpl_step_t STEPS_BIG[] TFW_PROGMEM = {
    { TFW_TMPL_RAM | 0u, 0u, 200u },
    { TFW_TMPL_RAM | 0u, 0u, 200u },
};

static void test_window_past_255_bytes(void)
{
    const uint8_t *src[1];
    uint8_t        buf[16];
    uint8_t        want[TFW_SHA256_LEN];
    uint8_t        got[TFW_SHA256_LEN];
    tfw_sha256_t   c;
    tfw_emit_t     e;
    uint16_t       i;

    for (i = 0u; i < sizeof BIG; i++) {
        BIG[i] = (uint8_t)(i * 7u + 3u);
    }
    src[0] = BIG;

    memset(&e, 0, sizeof e);
    e.out  = buf;
    e.from = 395u;
    e.cap  = 10u;
    tfw_tmpl_emit(&e, STEPS_BIG, 2u, src);
    TEST_ASSERT_EQUAL_UINT16(400u, e.pos);
    TEST_ASSERT_EQUAL_UINT8(5u, e.got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BIG + 195, buf, 5u);

    /* Across the seam between the two steps. */
    memset(&e, 0, sizeof e);
    e.out  = buf;
    e.from = 198u;
    e.cap  = 4u;
    tfw_tmpl_emit(&e, STEPS_BIG, 2u, src);
    TEST_ASSERT_EQUAL_UINT8(4u, e.got);
    TEST_ASSERT_EQUAL_HEX8(BIG[198], buf[0]);
    TEST_ASSERT_EQUAL_HEX8(BIG[199], buf[1]);
    TEST_ASSERT_EQUAL_HEX8(BIG[0], buf[2]);
    TEST_ASSERT_EQUAL_HEX8(BIG[1], buf[3]);

    tfw_sha256_init(&c);
    tfw_sha256_update(&c, BIG, sizeof BIG);
    tfw_sha256_update(&c, BIG, sizeof BIG);
    tfw_sha256_final(&c, want);
    tfw_tmpl_digest(STEPS_BIG, 2u, src, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, TFW_SHA256_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_whole_document_into_a_large_buffer);
    RUN_TEST(test_window_named_edges);
    RUN_TEST(test_every_window_is_a_slice_of_the_whole);
    RUN_TEST(test_hash_only_is_sha256_of_the_whole);
    RUN_TEST(test_hash_and_window_together);
    RUN_TEST(test_digest_equals_the_hash_only_walk);
    RUN_TEST(test_empty_table_emits_nothing);
    RUN_TEST(test_kind_pgm_is_verbatim_from_offset);
    RUN_TEST(test_kind_ram_is_verbatim);
    RUN_TEST(test_kind_hex_is_two_lowercase_digits_per_byte);
    RUN_TEST(test_kind_hex_window_splits_a_byte);
    RUN_TEST(test_kind_pos_sets_0x40_and_clears_the_top_bit);
    RUN_TEST(test_source_index_fifteen);
    RUN_TEST(test_window_past_255_bytes);
    return UNITY_END();
}
