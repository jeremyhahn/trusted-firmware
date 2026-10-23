/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The boot's size-first SHA-256, src/crypto/sha256_small.c, against seeded
 * mutants (#163). Each message is hashed in one call, split in two at every
 * offset, and fed a byte at a time, so a buffer or block-count mutant in
 * tfw_sha256_update cannot hide behind one call pattern. The length field is
 * pinned at its high word by a forged whole-block count (no message that long
 * fits a test), and HMAC at the key lengths either side of the block.
 *
 * Mutants tried, each killed by the test named:
 *   final: spill test `i > 56` -> `>= 56` / `> 57` -> the_padding_edges
 *   final: length high word `n >> 29` -> `>> 28` / `>> 30` / 0
 *                                              -> the_length_field_high_word
 *   final: low word `n << 3` -> `<< 2`, buflen dropped from n
 *                                              -> the_padding_edges
 *   final: 0x80 -> 0x00, padding memset short by one
 *                                              -> the_padding_edges, the_nist_vectors
 *   final: digest store past 32 bytes          -> the_digest_is_32_bytes
 *   update: block count +64 -> +63, buflen not reset, transform at 63
 *                                              -> the_padding_edges
 *   update: `i < len` -> `i <= len` / `i + 1 < len`
 *                                              -> the_padding_edges
 *   transform: schedule index, a sigma amount, Ch/Maj form, K or H0 entry
 *                                              -> the_nist_vectors
 *   rsh: byte step `x << 24` -> `x << 23`, bit step `x << 31` dropped
 *                                              -> the_nist_vectors
 *   sigma: last term rotates (`k < 2u` -> `k < 3u`)
 *                                              -> the_nist_vectors
 *   hmac: `keylen > 64` -> `>= 64` / `> 65`     -> the_hmac_key_length_edges
 *   hmac: long key copied, not hashed          -> the_hmac_key_length_edges
 *   hmac: ipad/opad swapped, inner digest 31 bytes
 *                                              -> the_hmac_vectors
 *   hmac: key not zeroed before the copy       -> the_hmac_vectors (a stack
 *     painted 0xA5 first, so the short key's tail is not zero by luck)
 * Equivalent, so not killable from outside: rsh's byte step `n >= 8` ->
 * `n > 8` (eight single-bit steps are one byte step); the closing memsets of
 * k, pad and inner in tfw_hmac_sha256 clear stack never read again.
 *
 * Every expected digest is a literal: NIST's FIPS 180-4 examples, RFC 4231
 * test case 1, or computed outside this code (Python's hashlib/hmac, and for
 * the forged length a FIPS 180-4 compression written in Python and checked
 * against hashlib).
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "sha256.h"

/* SHA-256(""), FIPS 180-4 example. */
static const uint8_t SHA_EMPTY[32] = {
    0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
    0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
};

/* SHA-256("abc"), FIPS 180-4 example. */
static const uint8_t SHA_ABC[32] = {
    0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
    0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
};

/* FIPS 180-4's two-block example: 448 bits, so the length spills. */
static const char NIST_448[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const uint8_t SHA_448[32] = {
    0x24, 0x8D, 0x6A, 0x61, 0xD2, 0x06, 0x38, 0xB8, 0xE5, 0xC0, 0x26, 0x93, 0x0C, 0x3E, 0x60, 0x39,
    0xA3, 0x3C, 0xE4, 0x59, 0x64, 0xFF, 0x21, 0x67, 0xF6, 0xEC, 0xED, 0xD4, 0x19, 0xDB, 0x06, 0xC1
};

/* SHA-256('a' x n) at the padding edges of the first and second block. */
typedef struct { uint8_t n; uint8_t digest[32]; } edge_t;
static const edge_t EDGES[] = {
    { 55u, { 0x9F, 0x43, 0x90, 0xF8, 0xD3, 0x0C, 0x2D, 0xD9, 0x2E, 0xC9, 0xF0, 0x95, 0xB6, 0x5E, 0x2B, 0x9A,
             0xE9, 0xB0, 0xA9, 0x25, 0xA5, 0x25, 0x8E, 0x24, 0x1C, 0x9F, 0x1E, 0x91, 0x0F, 0x73, 0x43, 0x18 } },
    { 56u, { 0xB3, 0x54, 0x39, 0xA4, 0xAC, 0x6F, 0x09, 0x48, 0xB6, 0xD6, 0xF9, 0xE3, 0xC6, 0xAF, 0x0F, 0x5F,
             0x59, 0x0C, 0xE2, 0x0F, 0x1B, 0xDE, 0x70, 0x90, 0xEF, 0x79, 0x70, 0x68, 0x6E, 0xC6, 0x73, 0x8A } },
    { 63u, { 0x7D, 0x3E, 0x74, 0xA0, 0x5D, 0x7D, 0xB1, 0x5B, 0xCE, 0x4A, 0xD9, 0xEC, 0x06, 0x58, 0xEA, 0x98,
             0xE3, 0xF0, 0x6E, 0xEE, 0xCF, 0x16, 0xB4, 0xC6, 0xFF, 0xF2, 0xDA, 0x45, 0x7D, 0xDC, 0x2F, 0x34 } },
    { 64u, { 0xFF, 0xE0, 0x54, 0xFE, 0x7A, 0xE0, 0xCB, 0x6D, 0xC6, 0x5C, 0x3A, 0xF9, 0xB6, 0x1D, 0x52, 0x09,
             0xF4, 0x39, 0x85, 0x1D, 0xB4, 0x3D, 0x0B, 0xA5, 0x99, 0x73, 0x37, 0xDF, 0x15, 0x46, 0x68, 0xEB } },
    { 119u, { 0x31, 0xEB, 0xA5, 0x1C, 0x31, 0x3A, 0x5C, 0x08, 0x22, 0x6A, 0xDF, 0x18, 0xD4, 0xA3, 0x59, 0xCF,
              0xDF, 0xD8, 0xD2, 0xE8, 0x16, 0xB1, 0x3F, 0x4A, 0xF9, 0x52, 0xF7, 0xEA, 0x65, 0x84, 0xDC, 0xFB } },
    { 120u, { 0x2F, 0x3D, 0x33, 0x54, 0x32, 0xC7, 0x0B, 0x58, 0x0A, 0xF0, 0xE8, 0xE1, 0xB3, 0x67, 0x4A, 0x7C,
              0x02, 0x0D, 0x68, 0x3A, 0xA5, 0xF7, 0x3A, 0xAA, 0xED, 0xFD, 0xC5, 0x5A, 0xF9, 0x04, 0xC2, 0x1C } },
    { 128u, { 0x68, 0x36, 0xCF, 0x13, 0xBA, 0xC4, 0x00, 0xE9, 0x10, 0x50, 0x71, 0xCD, 0x6A, 0xF4, 0x70, 0x84,
              0xDF, 0xAC, 0xAD, 0x4E, 0x5E, 0x30, 0x2C, 0x94, 0xBF, 0xED, 0x24, 0xE0, 0x13, 0xAF, 0xB7, 0x3E } },
};

/* One final block, no data, after a forged whole-block count of n bytes: the
 * length field is n * 8 in 64 bits, its high word n >> 29. */
static const uint8_t FORGED_2_29[32] = {   /* n = 2^29: high word 1, low word 0 */
    0x06, 0x31, 0xD3, 0x40, 0x41, 0xC9, 0x2E, 0x76, 0xB5, 0x84, 0xF8, 0x92, 0x29, 0x43, 0x52, 0x69,
    0x82, 0xF9, 0x05, 0x3A, 0x1B, 0x78, 0x95, 0xAE, 0xAE, 0x06, 0xF6, 0x41, 0xDF, 0x90, 0x53, 0x70
};
static const uint8_t FORGED_TOP[32] = {    /* n = 2^32 - 64: high word 7 */
    0xA2, 0xF5, 0x85, 0x89, 0x42, 0xE3, 0xE6, 0xB9, 0x9A, 0xE2, 0xBC, 0x5B, 0x44, 0x34, 0xE2, 0xBD,
    0xB4, 0xB1, 0xFA, 0x13, 0x89, 0xE0, 0xED, 0xE8, 0x2D, 0x83, 0x33, 0xA9, 0x4D, 0x85, 0x2B, 0x8B
};

/* HMAC-SHA256(key[i] = i + 1, "abc") at key lengths 63, 64 and 65. */
static const uint8_t HMAC_K63[32] = {
    0x60, 0x39, 0x2D, 0xB2, 0x65, 0xC7, 0x49, 0x94, 0x38, 0x61, 0x9E, 0xCF, 0xDA, 0x34, 0x0C, 0xEA,
    0xE2, 0x8C, 0x87, 0x35, 0xD8, 0x6E, 0x18, 0xC5, 0xAC, 0x41, 0xFE, 0x53, 0xA6, 0x00, 0x4E, 0xCA
};
static const uint8_t HMAC_K64[32] = {
    0xC9, 0x5C, 0xF0, 0x3C, 0xAC, 0x98, 0xF9, 0x94, 0x00, 0x86, 0x4D, 0x4D, 0x13, 0xC1, 0x3B, 0x60,
    0x6A, 0x94, 0x22, 0x6B, 0xE1, 0xEB, 0x90, 0xB1, 0x32, 0x9C, 0x9C, 0x4B, 0xDF, 0xC7, 0xC1, 0x49
};
static const uint8_t HMAC_K65[32] = {
    0xD0, 0x04, 0x73, 0xB0, 0x37, 0x97, 0xC3, 0x96, 0xD6, 0xCB, 0x45, 0x3F, 0x4C, 0x01, 0x38, 0x12,
    0x3E, 0x61, 0xD3, 0x25, 0x83, 0x01, 0x61, 0xB7, 0xEB, 0xD6, 0xCE, 0xD1, 0xFD, 0x38, 0x13, 0xF6
};

/* RFC 4231 test case 1: key 0x0b x 20, "Hi There". */
static const uint8_t HMAC_RFC4231_1[32] = {
    0xB0, 0x34, 0x4C, 0x61, 0xD8, 0xDB, 0x38, 0x53, 0x5C, 0xA8, 0xAF, 0xCE, 0xAF, 0x0B, 0xF1, 0x2B,
    0x88, 0x1D, 0xC2, 0x00, 0xC9, 0x83, 0x3D, 0xA7, 0x26, 0xE9, 0x37, 0x6C, 0x2E, 0x32, 0xCF, 0xF7
};

/* HMAC-SHA256("", ""). */
static const uint8_t HMAC_EMPTY[32] = {
    0xB6, 0x13, 0x67, 0x9A, 0x08, 0x14, 0xD9, 0xEC, 0x77, 0x2F, 0x95, 0xD7, 0x78, 0xC3, 0x5F, 0xC5,
    0xFF, 0x16, 0x97, 0xC4, 0x93, 0x71, 0x56, 0x53, 0xC6, 0xC7, 0x12, 0x14, 0x42, 0x92, 0xC5, 0xAD
};

static const uint8_t ABC[3] = { 'a', 'b', 'c' };

void setUp(void) {}
void tearDown(void) {}

/* One call, split in two at every offset, and a byte at a time. */
static void check_every_way(const uint8_t *msg, uint16_t len, const uint8_t expect[32])
{
    tfw_sha256_t c;
    uint8_t      out[32];
    uint16_t     cut, i;

    tfw_sha256(msg, len, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expect, out, 32, "one call");
    for (cut = 0u; cut <= len; cut++) {
        tfw_sha256_init(&c);
        tfw_sha256_update(&c, msg, cut);
        tfw_sha256_update(&c, msg + cut, (uint16_t)(len - cut));
        tfw_sha256_final(&c, out);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expect, out, 32, "split");
    }
    tfw_sha256_init(&c);
    for (i = 0u; i < len; i++) {
        tfw_sha256_update(&c, msg + i, 1u);
    }
    tfw_sha256_final(&c, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expect, out, 32, "a byte at a time");
}

/* A stack frame below the callee painted, so a buffer the code under test
 * forgets to clear holds 0xA5 rather than a lucky zero. */
static void paint_stack(void)
{
    volatile uint8_t junk[1024];

    for (uint16_t i = 0u; i < sizeof junk; i++) { junk[i] = 0xA5u; }
}

static void test_the_nist_vectors(void)
{
    check_every_way(ABC, 0u, SHA_EMPTY);
    check_every_way(ABC, sizeof ABC, SHA_ABC);
    check_every_way((const uint8_t *)NIST_448, (uint16_t)(sizeof NIST_448 - 1u), SHA_448);
}

static void test_the_padding_edges(void)
{
    uint8_t msg[128];

    memset(msg, 'a', sizeof msg);
    for (uint8_t i = 0u; i < sizeof EDGES / sizeof EDGES[0]; i++) {
        check_every_way(msg, EDGES[i].n, EDGES[i].digest);
    }
}

static void test_the_length_field_high_word(void)
{
    tfw_sha256_t c;
    uint8_t      out[32];

    tfw_sha256_init(&c);
    c.bitlen = 0x20000000u;
    tfw_sha256_final(&c, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FORGED_2_29, out, 32);

    tfw_sha256_init(&c);
    c.bitlen = 0xFFFFFFC0u;
    tfw_sha256_final(&c, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FORGED_TOP, out, 32);
}

static void test_the_digest_is_32_bytes(void)
{
    uint8_t out[32 + 16];

    memset(out, 0xA5, sizeof out);
    tfw_sha256(ABC, sizeof ABC, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_ABC, out, 32);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out + 32, 16);

    memset(out, 0xA5, sizeof out);
    tfw_hmac_sha256(ABC, sizeof ABC, ABC, sizeof ABC, out);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out + 32, 16);
}

static void test_the_hmac_key_length_edges(void)
{
    uint8_t key[65];
    uint8_t out[32];

    for (uint8_t i = 0u; i < sizeof key; i++) { key[i] = (uint8_t)(i + 1u); }
    paint_stack();
    tfw_hmac_sha256(key, 63u, ABC, sizeof ABC, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(HMAC_K63, out, 32, "63-byte key");
    paint_stack();
    tfw_hmac_sha256(key, 64u, ABC, sizeof ABC, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(HMAC_K64, out, 32, "64-byte key, used as is");
    paint_stack();
    tfw_hmac_sha256(key, 65u, ABC, sizeof ABC, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(HMAC_K65, out, 32, "65-byte key, hashed first");
}

static void test_the_hmac_vectors(void)
{
    static const uint8_t HI[8] = { 'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e' };
    uint8_t key[20];
    uint8_t out[32];

    memset(key, 0x0B, sizeof key);
    paint_stack();
    tfw_hmac_sha256(key, sizeof key, HI, sizeof HI, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HMAC_RFC4231_1, out, 32);
    paint_stack();
    tfw_hmac_sha256(key, 0u, HI, 0u, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HMAC_EMPTY, out, 32);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_nist_vectors);
    RUN_TEST(test_the_padding_edges);
    RUN_TEST(test_the_length_field_high_word);
    RUN_TEST(test_the_digest_is_32_bytes);
    RUN_TEST(test_the_hmac_key_length_edges);
    RUN_TEST(test_the_hmac_vectors);
    return UNITY_END();
}
