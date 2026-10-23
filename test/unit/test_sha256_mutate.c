/* SHA-256 under mutation (issue #163): every length 0..300, every split point,
 * every one-bit flip.
 *
 * The same file builds twice: test_sha256_mutate links src/crypto/sha256.c
 * through the generic host rule, test_sha256_small_mutate links the soft
 * tfw-boot's src/crypto/sha256_small.c (mk/host.mk). The two implementations
 * define one API and cannot share a link, so they agree through a literal:
 * SUM_DIGEST is the SHA-256 of the 301 digests of MSG[0..L), L = 0..300, taken
 * from an independent implementation (Python's hashlib), never from this code.
 * Both binaries must reach it.
 *
 * Every message is copied to a heap buffer of exactly its length, so under
 * `make test-asan` a read past the end is a sanitizer report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "sha256.h"

#define MAX_LEN 300u

/* sha256("abc"), FIPS 180-4 -- the known-answer anchor. */
static const uint8_t ABC_DIGEST[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};

/* sha256(sha256(MSG[0..0)) || sha256(MSG[0..1)) || ... || sha256(MSG[0..300))),
 * MSG[i] = (i * 131 + 7) mod 256, by Python's hashlib. */
static const uint8_t SUM_DIGEST[32] = {
    0x77, 0x22, 0x02, 0x43, 0x65, 0xd2, 0x78, 0x36, 0x07, 0x9c, 0xbf, 0x29, 0xde, 0x35, 0xd0, 0x60,
    0xb9, 0x38, 0x3d, 0x7c, 0x71, 0x30, 0x83, 0x19, 0x96, 0x61, 0x39, 0x2f, 0x98, 0x32, 0x16, 0xd4,
};

static uint8_t g_msg[MAX_LEN];

void setUp(void)
{
    unsigned i;

    for (i = 0u; i < MAX_LEN; i++) {
        g_msg[i] = (uint8_t)(i * 131u + 7u);
    }
}

void tearDown(void) {}

/* A heap copy of exactly len bytes; one byte for len 0 so the pointer is real. */
static uint8_t *heap_copy(const uint8_t *p, unsigned len)
{
    uint8_t *h = (uint8_t *)malloc(len ? len : 1u);

    TEST_ASSERT_NOT_NULL(h);
    if (len) {
        memcpy(h, p, len);
    }
    return h;
}

static void test_sha256_mutate_known_answer_still_holds(void)
{
    uint8_t *m = heap_copy((const uint8_t *)"abc", 3u);
    uint8_t  out[32];

    tfw_sha256(m, 3u, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, 32);
    free(m);
}

static void test_sha256_mutate_every_length_matches_the_reference(void)
{
    tfw_sha256_t sum;
    uint8_t      d[32];
    unsigned     len;

    tfw_sha256_init(&sum);
    for (len = 0u; len <= MAX_LEN; len++) {
        uint8_t *m = heap_copy(g_msg, len);

        tfw_sha256(m, (uint16_t)len, d);
        tfw_sha256_update(&sum, d, 32u);
        free(m);
    }
    tfw_sha256_final(&sum, d);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SUM_DIGEST, d, 32);
}

static void test_sha256_mutate_incremental_equals_one_shot_at_every_split(void)
{
    unsigned len, k, i;

    for (len = 0u; len <= MAX_LEN; len++) {
        uint8_t     *m = heap_copy(g_msg, len);
        uint8_t      want[32], got[32];
        tfw_sha256_t c;

        tfw_sha256(m, (uint16_t)len, want);
        for (k = 0u; k <= len; k++) {
            tfw_sha256_init(&c);
            tfw_sha256_update(&c, m, (uint16_t)k);
            tfw_sha256_update(&c, m + k, (uint16_t)(len - k));
            tfw_sha256_final(&c, got);
            if (memcmp(want, got, 32) != 0) {
                printf("len %u split %u\n", len, k);
                TEST_FAIL_MESSAGE("two-part digest differs from one-shot");
            }
        }
        /* one byte at a time, and an empty update between each */
        tfw_sha256_init(&c);
        for (i = 0u; i < len; i++) {
            tfw_sha256_update(&c, m + i, 1u);
            tfw_sha256_update(&c, m + i, 0u);
        }
        tfw_sha256_final(&c, got);
        if (memcmp(want, got, 32) != 0) {
            printf("len %u byte-at-a-time\n", len);
            TEST_FAIL_MESSAGE("byte-wise digest differs from one-shot");
        }
        free(m);
    }
}

/* At every length, every byte position takes a flip (bit (i + len) mod 8, so
 * each bit index is reached at every position over eight lengths); at the
 * block-edge lengths and the longest, every bit. All of them is 361k digests,
 * past the 10 s budget of sha256_small.c under the sanitizers. */
static int every_bit_at(unsigned len)
{
    return len == 1u || len == 55u || len == 56u || len == 63u || len == 64u ||
           len == 65u || len == 119u || len == 120u || len == 128u || len == MAX_LEN;
}

static void flip_check(uint8_t *m, unsigned len, unsigned bit, const uint8_t want[32])
{
    uint8_t got[32];

    m[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
    tfw_sha256(m, (uint16_t)len, got);
    m[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
    if (memcmp(want, got, 32) == 0) {
        printf("len %u bit %u\n", len, bit);
        TEST_FAIL_MESSAGE("a one-bit flip left the digest unchanged");
    }
}

static void test_sha256_mutate_every_one_bit_flip_changes_the_digest(void)
{
    unsigned len, i;

    for (len = 1u; len <= MAX_LEN; len++) {
        uint8_t *m = heap_copy(g_msg, len);
        uint8_t  want[32], got[32];

        tfw_sha256(m, (uint16_t)len, want);
        if (every_bit_at(len)) {
            for (i = 0u; i < len * 8u; i++) {
                flip_check(m, len, i, want);
            }
        } else {
            for (i = 0u; i < len; i++) {
                flip_check(m, len, i * 8u + (i + len) % 8u, want);
            }
        }
        /* the message is restored: its digest is back */
        tfw_sha256(m, (uint16_t)len, got);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
        free(m);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_mutate_known_answer_still_holds);
    RUN_TEST(test_sha256_mutate_every_length_matches_the_reference);
    RUN_TEST(test_sha256_mutate_incremental_equals_one_shot_at_every_split);
    RUN_TEST(test_sha256_mutate_every_one_bit_flip_changes_the_digest);
    return UNITY_END();
}
