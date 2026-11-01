/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The image header, mutated exhaustively (#163).
 *
 * Every byte of the trailer is set to every value, on the serialiser's
 * trailer and on each literal board vector, and every image_len from 0 to one
 * page past the maximum is parsed, with the powers of two either side of it.
 * Then a seeded run of multi-byte mutations.
 *
 * The oracle is written from the layout in include/tfw/boot.h, not from the
 * parser: a trailer is accepted exactly when its magic, its version, a non-zero
 * image_version and 0 < image_len <= TFW_BOOT_MAX_IMAGE_LEN all hold, and then
 * every field it hands back -- length, version, board, flags, measurement and
 * signature -- is the field in the bytes, never a different one. A refused
 * trailer writes nothing to *out. The binding answer is checked against the
 * same bytes: this board's id, never 0, and no unknown flag.
 *
 * Each trailer is parsed from a heap buffer of exactly TFW_BOOT_TRAILER_LEN
 * bytes, so under `make test-asan` a read past it is a fault, not a pass. The
 * seed is a literal and every failure prints it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "boot_image.h"
#include "vectors.h"

#define CASES 40000u
#define SEED  0x6A09E667u

static uint32_t rng;
static char why[96];

void setUp(void) {}
void tearDown(void) {}

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static const char *const spans[] = {
    TFWV_TRAILER_ATMEGA328P_SPAN,
    TFWV_TRAILER_ATMEGA2560_SPAN,
    TFWV_TRAILER_ATMEGA32U4_SPAN,
    TFWV_TRAILER_ATTINY85_SPAN,
    TFWV_TRAILER_ESP32P4POE_SPAN,
    TFWV_TRAILER_ESP32QEMU_SPAN,
    TFWV_TRAILER_MISMATCH_SPAN,
    TFWV_TRAILER_BOARDZERO_SPAN,
    TFWV_TRAILER_RESERVEDFLAG_SPAN,
    TFWV_TRAILER_DEVIMAGE_SPAN,
};
#define SPAN_COUNT ((uint32_t)(sizeof spans / sizeof spans[0]))
#define BASE_COUNT (SPAN_COUNT + 1u)

/* Base 0 is the serialiser's trailer for this board; the rest are the
 * literal vectors with a patterned signature after the signed span. */
static void base(uint32_t b, uint8_t raw[TFW_BOOT_TRAILER_LEN])
{
    if (b == 0u) {
        tfw_boot_trailer_t tr;
        memset(&tr, 0, sizeof tr);
        tr.image_version = 7u;
        tr.image_len     = 1024u;
        tr.board_id      = (uint8_t)TFW_BOARD_ID;
        for (uint32_t i = 0; i < TFW_BOOT_MEASURE_LEN; i++) { tr.measurement[i] = (uint8_t)(0xA0u + i); }
        for (uint32_t i = 0; i < TFW_BOOT_SIG_LEN; i++)     { tr.signature[i]   = (uint8_t)(0x30u ^ i); }
        tfw_boot_trailer_serialise(&tr, raw);
        return;
    }
    memset(raw, 0, TFW_BOOT_TRAILER_LEN);
    memcpy(raw, spans[b - 1u], TFWV_TRAILER_SPAN_LEN);
    for (uint32_t i = TFW_BOOT_TR_SIGNATURE; i < TFW_BOOT_TR_SIGNATURE + TFW_BOOT_SIG_LEN; i++) {
        raw[i] = (uint8_t)(i * 7u + b);
    }
}

/* The layout's answer, from the bytes alone. */
static int expect_ok(const uint8_t *raw)
{
    uint32_t len = rd32(&raw[TFW_BOOT_TR_IMAGE_LEN]);
    uint8_t  reserved = 0u;

    for (uint32_t i = TFW_BOOT_TR_RESERVED; i < TFW_BOOT_TRAILER_LEN; i++) {
        reserved |= raw[i];
    }
    return reserved == 0u && rd32(&raw[TFW_BOOT_TR_MAGIC]) == TFW_BOOT_TRAILER_MAGIC &&
           ((uint32_t)raw[TFW_BOOT_TR_VERSION] | ((uint32_t)raw[TFW_BOOT_TR_VERSION + 1u] << 8)) ==
               TFW_BOOT_TRAILER_VERSION &&
           rd32(&raw[TFW_BOOT_TR_IMAGE_VERSION]) != 0u &&
           len != 0u && len <= TFW_BOOT_MAX_IMAGE_LEN;
}

static int expect_bound(const uint8_t *raw)
{
    uint8_t board = raw[TFW_BOOT_TR_BOARD_ID];
    return board != 0u && board == (uint8_t)TFW_BOARD_ID &&
           (raw[TFW_BOOT_TR_IMAGE_FLAGS] & (uint8_t)~TFW_BOOT_IMAGE_FLAGS_KNOWN) == 0u;
}

/* Parse one trailer from a heap copy of exactly its length and hold the
 * answer to the oracle. Returns 1 when it was accepted. */
static int check(const uint8_t *src)
{
    uint8_t *raw = malloc(TFW_BOOT_TRAILER_LEN);
    tfw_boot_trailer_t out, before;

    TEST_ASSERT_NOT_NULL(raw);
    memcpy(raw, src, TFW_BOOT_TRAILER_LEN);
    memset(&out, 0xCC, sizeof out);
    memcpy(&before, &out, sizeof out);

    tfw_status_t rc = tfw_boot_trailer_parse(raw, &out);
    int ok = expect_ok(raw);

    if (!ok) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, rc, why);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &out, sizeof out, why);
        free(raw);
        return 0;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(TFW_OK, rc, why);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rd32(&raw[TFW_BOOT_TR_IMAGE_LEN]), out.image_len, why);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rd32(&raw[TFW_BOOT_TR_IMAGE_VERSION]), out.image_version, why);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(raw[TFW_BOOT_TR_BOARD_ID], out.board_id, why);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(raw[TFW_BOOT_TR_IMAGE_FLAGS], out.image_flags, why);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&raw[TFW_BOOT_TR_MEASUREMENT], out.measurement,
                                     TFW_BOOT_MEASURE_LEN, why);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&raw[TFW_BOOT_TR_SIGNATURE], out.signature,
                                     TFW_BOOT_SIG_LEN, why);
    TEST_ASSERT_EQUAL_INT_MESSAGE(expect_bound(raw) ? TFW_OK : TFW_ERR_UNSUPPORTED,
                                  tfw_boot_binding_permit(&out, (uint8_t)TFW_BOARD_ID), why);

    /* What was accepted serialises back to the bytes it came from, through the
     * end of the signature. Past it the trailer carries nothing. */
    uint8_t again[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_serialise(&out, again);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(raw, again, TFW_BOOT_TR_SIGNATURE + TFW_BOOT_SIG_LEN, why);
    free(raw);
    return 1;
}

static void test_every_byte_set_to_every_value(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    uint32_t accepted = 0, refused = 0;

    for (uint32_t b = 0; b < BASE_COUNT; b++) {
        for (uint32_t at = 0; at < TFW_BOOT_TRAILER_LEN; at++) {
            for (uint32_t v = 0; v < 256u; v++) {
                base(b, raw);
                raw[at] = (uint8_t)v;
                snprintf(why, sizeof why, "base %u byte %u value 0x%02X", b, at, v);
                if (check(raw)) { accepted++; } else { refused++; }
            }
        }
    }
    TEST_ASSERT_TRUE(accepted > 0u);
    TEST_ASSERT_TRUE(refused > 0u);
}

static void test_every_bit_flipped(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];

    /* A flip in the fields the parser judges must be judged; a flip anywhere
     * else is carried through unchanged, which check() pins byte for byte. */
    for (uint32_t b = 0; b < BASE_COUNT; b++) {
        for (uint32_t bit = 0; bit < TFW_BOOT_TRAILER_LEN * 8u; bit++) {
            base(b, raw);
            raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            snprintf(why, sizeof why, "base %u bit %u", b, bit);
            int ok = check(raw);
            if (bit / 8u < TFW_BOOT_TR_BOARD_ID) {
                TEST_ASSERT_FALSE_MESSAGE(ok, why);
            }
        }
    }
}

static void test_every_image_length(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    uint32_t accepted = 0;

    for (uint32_t len = 0; len <= TFW_BOOT_MAX_IMAGE_LEN + 256u; len++) {
        base(0u, raw);
        wr32(&raw[TFW_BOOT_TR_IMAGE_LEN], len);
        snprintf(why, sizeof why, "image_len %u", len);
        accepted += (uint32_t)check(raw);
    }
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_MAX_IMAGE_LEN, accepted);

    for (uint32_t k = 0; k < 32u; k++) {
        const uint32_t p = 1uL << k;
        const uint32_t around[] = { p - 1u, p, p + 1u, ~p, 0u - p };
        for (uint32_t i = 0; i < sizeof around / sizeof around[0]; i++) {
            base(0u, raw);
            wr32(&raw[TFW_BOOT_TR_IMAGE_LEN], around[i]);
            snprintf(why, sizeof why, "image_len 0x%08X", around[i]);
            (void)check(raw);
        }
    }
}

static void test_seeded_multi_byte_mutations(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    uint32_t accepted = 0;

    for (uint32_t c = 0; c < CASES; c++) {
        rng = SEED ^ (c * 0x9E3779B9u);
        if (rng == 0u) { rng = SEED; }
        base(next() % BASE_COUNT, raw);
        for (uint32_t k = 1u + next() % 6u; k > 0u; k--) {
            uint32_t at = next() % TFW_BOOT_TRAILER_LEN;
            switch (next() % 4u) {
            case 0:  raw[at] = (uint8_t)next(); break;
            case 1:  raw[at] ^= (uint8_t)(1u << (next() % 8u)); break;
            case 2:  wr32(&raw[TFW_BOOT_TR_IMAGE_LEN], TFW_BOOT_MAX_IMAGE_LEN + 2u - next() % 5u); break;
            default: raw[TFW_BOOT_TR_BOARD_ID + next() % 2u] = (uint8_t)next(); break;
            }
        }
        snprintf(why, sizeof why, "seed 0x%08X case %u", SEED, c);
        accepted += (uint32_t)check(raw);
    }
    TEST_ASSERT_TRUE(accepted > CASES / 10u);
    TEST_ASSERT_TRUE(accepted < CASES);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_byte_set_to_every_value);
    RUN_TEST(test_every_bit_flipped);
    RUN_TEST(test_every_image_length);
    RUN_TEST(test_seeded_multi_byte_mutations);
    return UNITY_END();
}
