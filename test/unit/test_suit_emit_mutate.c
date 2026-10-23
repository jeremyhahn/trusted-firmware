/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutation tests for the build's SUIT emitter (tools/suit-emit/emit.c, #78,
 * #163). Each test kills a named fault the vector tests in test_suit_emit.c
 * let through: a CBOR head bound moved by one, a capacity check that is off by
 * one or drops the length, a 64 KiB bound that refuses the largest manifest it
 * can hash. Every expected byte is the vector's or a literal from RFC 8949 §3.
 */
#include <string.h>
#include "unity.h"
#include "emit.h"
#include "tfw/config.h"
#include "sha256.h"
#include "suit_vectors.h"

/* Where the signature sits in SUIT_ENVELOPE; test_suit.c pins the offset. */
#define SIG_OFFSET 57u
/* Where the payload digest sits in it: D8 6B A2 02 58 xx 82 58 24 82 0F 58 20. */
#define DIGEST_OFFSET 13u
/* Where the vector's bstr lengths sit: the manifest, common and shared. */
#define MAN_LEN    1u
#define COMMON_LEN 9u
#define SHARED_LEN 18u
/* The guard byte a write past the capacity would overwrite. */
#define GUARD      0xEEu

static const uint8_t vendor[16]   = TFW_SUIT_VENDOR_ID;
static const uint8_t class_id[16] = TFW_SUIT_CLASS_ID;

void setUp(void) {}
void tearDown(void) {}

/* The vector's manifest with seq 7 at [6] replaced by `enc`, every byte after
 * it shifted and the manifest length grown by the difference. */
static void expect_seq(uint32_t seq, const uint8_t *enc, size_t w)
{
    uint8_t out[256];
    size_t  n = tfw_suit_emit_manifest(out, sizeof out, SUIT_IMAGE_DIGEST,
                                       sizeof SUIT_IMAGE, seq, vendor, class_id);

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_MANIFEST - 1u + w, n);
    TEST_ASSERT_EQUAL_HEX8(SUIT_MANIFEST[0], out[0]);
    TEST_ASSERT_EQUAL_HEX8(SUIT_MANIFEST[MAN_LEN] - 1u + w, out[MAN_LEN]);
    TEST_ASSERT_EQUAL_MEMORY(&SUIT_MANIFEST[2], &out[2], 4u);
    TEST_ASSERT_EQUAL_MEMORY(enc, &out[6], w);
    TEST_ASSERT_EQUAL_MEMORY(&SUIT_MANIFEST[7], &out[6u + w], sizeof SUIT_MANIFEST - 7u);
}

/* The vector's manifest with the image size 18 40 at [98] replaced by `enc`,
 * and all three enclosing bstr lengths grown by the difference. */
static void expect_size(uint32_t size, const uint8_t *enc, size_t w)
{
    uint8_t out[256];
    size_t  n = tfw_suit_emit_manifest(out, sizeof out, SUIT_IMAGE_DIGEST,
                                       size, 7u, vendor, class_id);

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_MANIFEST - 2u + w, n);
    TEST_ASSERT_EQUAL_HEX8(SUIT_MANIFEST[MAN_LEN] - 2u + w, out[MAN_LEN]);
    TEST_ASSERT_EQUAL_HEX8(SUIT_MANIFEST[COMMON_LEN] - 2u + w, out[COMMON_LEN]);
    TEST_ASSERT_EQUAL_HEX8(SUIT_MANIFEST[SHARED_LEN] - 2u + w, out[SHARED_LEN]);
    TEST_ASSERT_EQUAL_HEX8(SUIT_MANIFEST[0], out[0]);
    TEST_ASSERT_EQUAL_MEMORY(&SUIT_MANIFEST[2], &out[2], COMMON_LEN - 2u);
    TEST_ASSERT_EQUAL_MEMORY(&SUIT_MANIFEST[COMMON_LEN + 1u], &out[COMMON_LEN + 1u],
                             SHARED_LEN - COMMON_LEN - 1u);
    TEST_ASSERT_EQUAL_MEMORY(&SUIT_MANIFEST[SHARED_LEN + 1u], &out[SHARED_LEN + 1u],
                             98u - SHARED_LEN - 1u);
    TEST_ASSERT_EQUAL_MEMORY(enc, &out[98], w);
}

/* Mutants: `v < 24` -> `v <= 24`, `v <= 0xFF` -> `v < 0xFF`, `v <= 0xFFFF` ->
 * `v < 0xFFFF`, and each of those the other way. Each bound is pinned from
 * both sides with the sequence number, a uint the manifest carries as-is. */
static void test_seq_heads_at_every_width_boundary(void)
{
    static const uint8_t s23[1]    = { 0x17 };
    static const uint8_t s24[2]    = { 0x18, 0x18 };
    static const uint8_t s255[2]   = { 0x18, 0xFF };
    static const uint8_t s256[3]   = { 0x19, 0x01, 0x00 };
    static const uint8_t s65535[3] = { 0x19, 0xFF, 0xFF };
    static const uint8_t s65536[5] = { 0x1A, 0x00, 0x01, 0x00, 0x00 };
    static const uint8_t smax[5]   = { 0x1A, 0xFF, 0xFF, 0xFF, 0xFF };

    expect_seq(23u, s23, sizeof s23);
    expect_seq(24u, s24, sizeof s24);
    expect_seq(255u, s255, sizeof s255);
    expect_seq(256u, s256, sizeof s256);
    expect_seq(65535u, s65535, sizeof s65535);
    expect_seq(65536uL, s65536, sizeof s65536);
    expect_seq(0xFFFFFFFFuL, smax, sizeof smax);
}

/* The same bounds on the image size, which sits three bstrs deep: a head of
 * the wrong width there moves three lengths, not one. */
static void test_size_heads_at_every_width_boundary(void)
{
    static const uint8_t z0[1]     = { 0x00 };
    static const uint8_t z23[1]    = { 0x17 };
    static const uint8_t z24[2]    = { 0x18, 0x18 };
    static const uint8_t z255[2]   = { 0x18, 0xFF };
    static const uint8_t z256[3]   = { 0x19, 0x01, 0x00 };
    static const uint8_t z4660[3]  = { 0x19, 0x12, 0x34 };
    static const uint8_t z65535[3] = { 0x19, 0xFF, 0xFF };
    static const uint8_t z65536[5] = { 0x1A, 0x00, 0x01, 0x00, 0x00 };

    expect_size(0u, z0, sizeof z0);
    expect_size(23u, z23, sizeof z23);
    expect_size(24u, z24, sizeof z24);
    expect_size(255u, z255, sizeof z255);
    expect_size(256u, z256, sizeof z256);
    expect_size(0x1234u, z4660, sizeof z4660);
    expect_size(65535u, z65535, sizeof z65535);
    expect_size(65536uL, z65536, sizeof z65536);
}

/* Mutants: put's `<= cap` -> `< cap` (the last byte is counted, not written)
 * and done's `<= cap` -> `< cap` (an exact fit reads as an overflow). Each
 * output at exactly its own size, into a buffer that would show either. */
static void test_an_exact_fit_is_written_whole(void)
{
    uint8_t out[sizeof SUIT_ENVELOPE + 1u];

    memset(out, GUARD, sizeof out);
    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_MANIFEST,
                             tfw_suit_emit_manifest(out, sizeof SUIT_MANIFEST, SUIT_IMAGE_DIGEST,
                                                    sizeof SUIT_IMAGE, 7u, vendor, class_id));
    TEST_ASSERT_EQUAL_MEMORY(SUIT_MANIFEST, out, sizeof SUIT_MANIFEST);
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_MANIFEST]);

    memset(out, GUARD, sizeof out);
    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_TBS,
                             tfw_suit_emit_tbs(out, sizeof SUIT_TBS,
                                               SUIT_MANIFEST, sizeof SUIT_MANIFEST));
    TEST_ASSERT_EQUAL_MEMORY(SUIT_TBS, out, sizeof SUIT_TBS);
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_TBS]);

    memset(out, GUARD, sizeof out);
    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_ENVELOPE,
                             tfw_suit_emit_envelope(out, sizeof SUIT_ENVELOPE, SUIT_MANIFEST,
                                                    sizeof SUIT_MANIFEST,
                                                    &SUIT_ENVELOPE[SIG_OFFSET]));
    TEST_ASSERT_EQUAL_MEMORY(SUIT_ENVELOPE, out, sizeof SUIT_ENVELOPE);
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_ENVELOPE]);
}

/* Mutants: put's `b->n + len <= cap` -> `b->n <= cap` (a write that starts
 * inside the buffer runs past it), and the length counted only when written
 * (a short buffer returns a short, nonzero length). One byte short of each
 * output: nothing at or past the capacity is touched and the answer is 0. */
static void test_one_byte_short_writes_nothing_past_the_capacity(void)
{
    uint8_t out[sizeof SUIT_ENVELOPE + 1u];

    memset(out, GUARD, sizeof out);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_manifest(out, sizeof SUIT_MANIFEST - 1u,
                                                        SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE,
                                                        7u, vendor, class_id));
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_MANIFEST - 1u]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_MANIFEST]);

    memset(out, GUARD, sizeof out);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_tbs(out, sizeof SUIT_TBS - 1u,
                                                   SUIT_MANIFEST, sizeof SUIT_MANIFEST));
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_TBS - 1u]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_TBS]);

    memset(out, GUARD, sizeof out);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_envelope(out, sizeof SUIT_ENVELOPE - 1u,
                                                        SUIT_MANIFEST, sizeof SUIT_MANIFEST,
                                                        &SUIT_ENVELOPE[SIG_OFFSET]));
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_ENVELOPE - 1u]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[sizeof SUIT_ENVELOPE]);
}

/* Mutant: payload's `> 0xFFFF` -> `>= 0xFFFF`. The largest manifest the
 * 16-bit hash length takes is signed over and carried whole. */
static void test_a_manifest_of_0xffff_bytes_is_taken(void)
{
    static uint8_t big[0xFFFF];
    static uint8_t out[sizeof SUIT_ENVELOPE - sizeof SUIT_MANIFEST + sizeof big];
    tfw_sha256_t   ctx;
    uint8_t        h[32];
    size_t         n;

    memset(big, 0x5A, sizeof big);
    tfw_sha256_init(&ctx);
    tfw_sha256_update(&ctx, big, (uint16_t)sizeof big);
    tfw_sha256_final(&ctx, h);

    n = tfw_suit_emit_tbs(out, sizeof out, big, sizeof big);
    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_TBS, n);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_TBS, out, sizeof SUIT_TBS - 32u);
    TEST_ASSERT_EQUAL_MEMORY(h, &out[sizeof SUIT_TBS - 32u], 32u);

    n = tfw_suit_emit_envelope(out, sizeof out, big, sizeof big, &SUIT_ENVELOPE[SIG_OFFSET]);
    TEST_ASSERT_EQUAL_size_t(sizeof out, n);
    TEST_ASSERT_EQUAL_MEMORY(big, &out[sizeof out - sizeof big], sizeof big);
}

/* Mutant: the payload digest taken over something other than the manifest
 * (or not at all). An empty manifest's payload is SHA-256 of nothing, FIPS
 * 180-4's e3b0c442..., and its envelope carries no manifest bytes. */
static void test_an_empty_manifest_signs_the_empty_digest(void)
{
    static const uint8_t empty[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
        0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
    };
    uint8_t out[sizeof SUIT_ENVELOPE];

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_TBS,
                             tfw_suit_emit_tbs(out, sizeof out, SUIT_MANIFEST, 0u));
    TEST_ASSERT_EQUAL_MEMORY(SUIT_TBS, out, sizeof SUIT_TBS - 32u);
    TEST_ASSERT_EQUAL_MEMORY(empty, &out[sizeof SUIT_TBS - 32u], 32u);

    TEST_ASSERT_EQUAL_size_t(sizeof SUIT_ENVELOPE - sizeof SUIT_MANIFEST,
                             tfw_suit_emit_envelope(out, sizeof out, SUIT_MANIFEST, 0u,
                                                    &SUIT_ENVELOPE[SIG_OFFSET]));
    TEST_ASSERT_EQUAL_MEMORY(empty, &out[DIGEST_OFFSET], 32u);
}

/* Mutant: tfw_suit_emit_envelope's (or _tbs's) refusal of a manifest over
 * 0xFFFF bytes dropped. Into an output with room for all of it, so the
 * refusal and not the capacity is what answers 0. */
static void test_a_manifest_over_0xffff_is_refused_with_room_for_it(void)
{
    static uint8_t big[0x10000];
    static uint8_t out[sizeof SUIT_ENVELOPE + sizeof big];

    memset(out, GUARD, sizeof out);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_tbs(out, sizeof out, big, sizeof big));
    TEST_ASSERT_EQUAL_size_t(0u, tfw_suit_emit_envelope(out, sizeof out, big, sizeof big,
                                                        &SUIT_ENVELOPE[SIG_OFFSET]));
    TEST_ASSERT_EQUAL_HEX8(GUARD, out[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_seq_heads_at_every_width_boundary);
    RUN_TEST(test_size_heads_at_every_width_boundary);
    RUN_TEST(test_an_exact_fit_is_written_whole);
    RUN_TEST(test_one_byte_short_writes_nothing_past_the_capacity);
    RUN_TEST(test_a_manifest_of_0xffff_bytes_is_taken);
    RUN_TEST(test_an_empty_manifest_signs_the_empty_digest);
    RUN_TEST(test_a_manifest_over_0xffff_is_refused_with_room_for_it);
    return UNITY_END();
}
