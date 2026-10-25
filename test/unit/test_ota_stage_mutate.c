/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OTA stager's records (src/core/ota_stage.c), mutated in the software NOR
 * part and read back through tfw_stage_marked: every byte of the MARK record
 * at every value, every MARK length, every bit of the staged stream the
 * stager checks, the MARK torn at every byte in its programming order, and
 * OTA_END/OTA_ACTIVATE cut at every transfer to the part. The one contract:
 * a damaged record is never answered as a staged image -- TFW_OK only ever
 * comes with the record exactly as written and the image's measurement.
 * Exhaustive, so there is no seed; each failure names its case.
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/board.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "ota_store.h"
#include "boot_platform.h"
#include "sha256.h"
#include "boot_measure.h"

#define TOTAL     512u
#define IMAGE     200u
#define TRAILER   128u
#define CHUNK     60u      /* TFW_MAX_PAYLOAD less the offset */
#define STAGE     ((uint32_t)TFW_OTA_STORE_STAGE_ADDR)
#define MARK      ((uint32_t)TFW_OTA_STORE_MARK_ADDR)
#define REC       TFW_STAGE_MARK_LEN
#define NOT_SET   0xA5A5A5A5u

static uint8_t img[TFW_OTA_STORE_STAGE_LEN];
static uint8_t meas[32];
static uint8_t rec[REC];   /* the MARK as the good round wrote it */
static char    why[96];

void setUp(void)    { tfw_nor_model_reset(); }
void tearDown(void) { }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* test_stage_marked.c's image: IMAGE bytes of pattern, the rest erased, and a
 * trailer whose image version is 3 and whose measurement is meas. */
static void build(void)
{
    tfw_sha256_t c;
    uint8_t     *t = &img[TOTAL - TRAILER];
    uint32_t     i;

    memset(img, 0xFF, sizeof img);
    for (i = 0u; i < IMAGE; i++) { img[i] = (uint8_t)(i * 7u + 3u); }
    tfw_sha256_init(&c);
    tfw_sha256_update(&c, img, (uint16_t)IMAGE);
    tfw_sha256_final(&c, meas);
    memset(t, 0, TRAILER);
    t[0] = 0x54u; t[1] = 0x46u; t[2] = 0x49u; t[3] = 0x4Du;   /* "TFIM" */
    t[4] = 0x01u; t[5] = 0x00u;                               /* version 1 */
    t[6] = 0x01u;                                             /* the 328P */
    put32(&t[TFW_BOOT_TR_IMAGE_VERSION], 3u);
    put32(&t[TFW_BOOT_TR_IMAGE_LEN], IMAGE);
    memcpy(&t[TFW_BOOT_TR_MEASUREMENT], meas, 32u);
}

static tfw_status_t call(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                         const uint8_t *in, uint8_t n, uint8_t *out)
{
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, 8u);
    return fn(&r, &w);
}

static tfw_status_t begin(void)
{
    uint8_t in[4], out[8];
    put32(in, TOTAL);
    return call(tfw_stage_cmd_begin, in, 4u, out);
}

static tfw_status_t data(uint32_t off, uint8_t n)
{
    uint8_t in[4u + CHUNK], out[8];
    put32(in, off);
    memcpy(&in[4], &img[off], n);
    return call(tfw_stage_cmd_data, in, (uint8_t)(4u + n), out);
}

static tfw_status_t end(void)
{
    uint8_t out[8];
    return call(tfw_stage_cmd_end, NULL, 0u, out);
}

static void stream(void)
{
    uint32_t off;
    for (off = 0u; off < TOTAL; off += CHUNK) {
        TEST_ASSERT_EQUAL(TFW_OK, data(off, (uint8_t)(TOTAL - off < CHUNK ? TOTAL - off : CHUNK)));
    }
}

/* A whole round, ended and activated, so the MARK record is written; rec is
 * what it wrote. */
static void good_round(void)
{
    build();
    TEST_ASSERT_EQUAL(TFW_OK, begin());
    stream();
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());
    memcpy(rec, &tfw_nor_model_mem()[MARK], REC);
}

static void poke(uint32_t addr, uint8_t v) { tfw_nor_model_load(addr, &v, 1u); }

/* tfw_stage_marked must refuse, both ways, and zero the version. */
static void expect_refused(void)
{
    uint8_t  h[TFW_BOOT_MEASURE_LEN];
    uint32_t v;
    uint8_t  full;

    for (full = 0u; full < 2u; full++) {
        v = NOT_SET;
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, tfw_stage_marked(h, &v, full), why);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, v, why);
    }
}

/* The contract after any damage: an answer of TFW_OK only with the MARK as the
 * good round wrote it, the image's measurement and version 3. */
static uint8_t check_marked(uint8_t full)
{
    uint8_t      h[TFW_BOOT_MEASURE_LEN];
    uint32_t     v = NOT_SET;
    tfw_status_t st = tfw_stage_marked(h, &v, full);

    if (st != TFW_OK) {
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, st, why);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, v, why);
        return 0u;
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(rec, &tfw_nor_model_mem()[MARK], REC, why);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(meas, h, 32u, why);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3u, v, why);
    return 1u;
}

static void test_good_round_is_marked(void)
{
    good_round();
    snprintf(why, sizeof why, "the good round");
    TEST_ASSERT_EQUAL_UINT8(1u, check_marked(0u));
    TEST_ASSERT_EQUAL_UINT8(1u, check_marked(1u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_stage_install_pending());
}

/* Every byte of the 40-byte MARK at each of its 255 other values. */
static void test_every_mark_byte_at_every_value_is_refused(void)
{
    uint32_t i;
    uint32_t v;

    good_round();
    for (i = 0u; i < REC; i++) {
        for (v = 0u; v < 256u; v++) {
            if (v == rec[i]) {
                continue;
            }
            snprintf(why, sizeof why, "mark byte %u = 0x%02X", (unsigned)i, (unsigned)v);
            poke(MARK + i, (uint8_t)v);
            expect_refused();
        }
        poke(MARK + i, rec[i]);
    }
    snprintf(why, sizeof why, "restored");
    TEST_ASSERT_EQUAL_UINT8(1u, check_marked(1u));
}

/* The MARK's length at every value from 0 past the end of the stage. */
static void test_every_mark_length_but_the_staged_one_is_refused(void)
{
    uint8_t  l[4];
    uint32_t len;

    good_round();
    for (len = 0u; len <= (uint32_t)TFW_OTA_STORE_STAGE_LEN + 2u * TFW_OTA_PAGE_MAX; len++) {
        if (len == TOTAL) {
            continue;
        }
        snprintf(why, sizeof why, "mark length %lu", (unsigned long)len);
        put32(l, len);
        tfw_nor_model_load(MARK + TFW_STAGE_MARK_O_LEN, l, 4u);
        expect_refused();
    }
    tfw_nor_model_load(MARK + TFW_STAGE_MARK_O_LEN, &rec[TFW_STAGE_MARK_O_LEN], 4u);
    snprintf(why, sizeof why, "restored");
    TEST_ASSERT_EQUAL_UINT8(1u, check_marked(1u));
}

/* Every bit of every staged byte the full check covers: the image, the
 * trailer's magic, version and board id, its image length and measurement.
 * The bytes it does not cover -- image_flags, image_version, the signature
 * and the erased gap -- are tfw-boot's to check, not the stager's. */
static void test_every_checked_stream_bit_is_refused_by_the_full_check(void)
{
    uint32_t p;
    uint32_t at;
    uint32_t off;
    uint8_t  bit;
    uint8_t  o;

    good_round();
    for (p = 0u; p < IMAGE + TRAILER; p++) {
        off = p - IMAGE;
        if (p >= IMAGE && ((off >= TFW_BOOT_TR_IMAGE_FLAGS && off < TFW_BOOT_TR_IMAGE_LEN) ||
                           off >= TFW_BOOT_TR_SIGNED_LEN)) {
            continue;
        }
        at = (p < IMAGE) ? STAGE + p : STAGE + TOTAL - TRAILER + off;
        o  = tfw_nor_model_mem()[at];
        for (bit = 0u; bit < 8u; bit++) {
            snprintf(why, sizeof why, "stream byte 0x%lX bit %u", (unsigned long)(at - STAGE), (unsigned)bit);
            poke(at, (uint8_t)(o ^ (1u << bit)));
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, check_marked(1u), why);
            /* The quick read takes the trailer's measurement alone. */
            if (p >= IMAGE && off >= TFW_BOOT_TR_MEASUREMENT) {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, check_marked(0u), why);
            }
        }
        poke(at, o);
    }
    snprintf(why, sizeof why, "restored");
    TEST_ASSERT_EQUAL_UINT8(1u, check_marked(1u));
}

/* mark() programs bytes 4..39, then the magic: a reset at any byte leaves a
 * prefix of that order, the byte it stopped in holding any value with fewer
 * bits programmed (NOR clears 1 to 0 only), and the rest erased. */
static void test_mark_torn_at_every_byte_is_never_marked(void)
{
    uint8_t  order[REC];
    uint8_t  erased[REC];
    uint32_t k;
    uint32_t i;
    uint32_t m;
    uint8_t  full;

    for (i = 0u; i < REC - 4u; i++) { order[i] = (uint8_t)(i + 4u); }
    for (i = 0u; i < 4u; i++) { order[REC - 4u + i] = (uint8_t)i; }
    memset(erased, 0xFF, sizeof erased);

    good_round();
    for (k = 0u; k < REC; k++) {
        for (m = 0u; m < 256u; m++) {
            tfw_nor_model_load(MARK, erased, REC);
            for (i = 0u; i < k; i++) { poke(MARK + order[i], rec[order[i]]); }
            poke(MARK + order[k], (uint8_t)(rec[order[k]] | m));
            for (full = 0u; full < 2u; full++) {
                snprintf(why, sizeof why, "torn at byte %u (order %u) mask 0x%02X full %u",
                         (unsigned)order[k], (unsigned)k, (unsigned)m, (unsigned)full);
                /* Only the last byte whole, at the last step, is the record. */
                TEST_ASSERT_EQUAL_UINT8_MESSAGE((k == REC - 1u && (rec[order[k]] | m) == rec[order[k]]) ? 1u : 0u,
                                                check_marked(full), why);
            }
        }
    }
}

/* Within the magic's own program the bytes may land in any order: of its 16
 * subsets only the whole one is a record. */
static void test_magic_programmed_in_any_subset_is_marked_only_whole(void)
{
    uint8_t  ff = 0xFFu;
    uint32_t s;
    uint32_t i;

    good_round();
    for (s = 0u; s < 16u; s++) {
        for (i = 0u; i < 4u; i++) {
            if ((s >> i) & 1u) {
                poke(MARK + i, rec[i]);
            } else {
                tfw_nor_model_load(MARK + i, &ff, 1u);
            }
        }
        snprintf(why, sizeof why, "magic subset 0x%X", (unsigned)s);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(s == 15u ? 1u : 0u, check_marked(0u), why);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(s == 15u ? 1u : 0u, check_marked(1u), why);
    }
}

/* OTA_END and OTA_ACTIVATE with the n-th transfer to the part failing, for
 * every n the two reach: whatever either answers, the store is marked only
 * if the record is whole, and only an activate that answered TFW_OK can be
 * relied on to have marked it. */
static void test_end_and_activate_cut_at_every_transfer(void)
{
    uint32_t     n;
    uint32_t     cut = 0u;
    tfw_status_t st;
    uint8_t      ok;

    good_round();   /* rec */
    for (n = 1u; n < 256u; n++) {
        tfw_nor_model_reset();
        tfw_stage_init();   /* a boot: the OTA_BEGIN budget is per boot (#232) */
        build();
        TEST_ASSERT_EQUAL(TFW_OK, begin());
        stream();
        tfw_nor_model_fail_xfer((uint8_t)n);
        st = end();
        if (st == TFW_OK) {
            st = tfw_stage_activate();
        }
        tfw_nor_model_fail_xfer(0u);
        snprintf(why, sizeof why, "cut at transfer %lu, answered %d", (unsigned long)n, (int)st);
        ok = check_marked(1u);
        if (st == TFW_OK) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(1u, ok, why);
        } else {
            cut++;
        }
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(ok, check_marked(0u), why);
    }
    TEST_ASSERT_TRUE(cut > 0u);     /* the cuts reached the part */
    TEST_ASSERT_TRUE(cut < 255u);   /* and past the round's last transfer */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_good_round_is_marked);
    RUN_TEST(test_every_mark_byte_at_every_value_is_refused);
    RUN_TEST(test_every_mark_length_but_the_staged_one_is_refused);
    RUN_TEST(test_every_checked_stream_bit_is_refused_by_the_full_check);
    RUN_TEST(test_mark_torn_at_every_byte_is_never_marked);
    RUN_TEST(test_magic_programmed_in_any_subset_is_marked_only_whole);
    RUN_TEST(test_end_and_activate_cut_at_every_transfer);
    return UNITY_END();
}
