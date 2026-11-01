/* CRC-16/CCITT-FALSE under mutation (issue #163): every one- and two-bit error
 * and every burst up to 16 bits in a frame-sized buffer is detected.
 *
 * Deterministic: the frame and the sampled burst interiors come from xorshift32
 * seeded with a literal, and a failure prints the seed and the error. Errors are
 * numbered in wire order, most significant bit of byte 0 first, the order the
 * CRC folds them -- so a "burst" here is a burst on the wire.
 *
 * The frame is a heap buffer of exactly its length, so under `make test-asan` a
 * read past the end is a sanitizer report. A prefix table of tfw_crc16_update
 * states keeps the two-bit sweep (557k frames) inside its time budget; every
 * CRC still runs through the linked code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "crc16.h"
#include "vectors.h"

#define FRAME_LEN 132u                  /* as test_crc16.c's frame-sized buffer */
#define NBITS     (FRAME_LEN * 8u)
#define SEED      0x1631C16Bu

static uint32_t rng;
static uint8_t *g_frame;
static uint16_t g_clean;
static uint16_t g_prefix[FRAME_LEN + 1u]; /* CRC state before byte i of the clean frame */

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void flip(uint8_t *p, unsigned bit)
{
    p[bit / 8u] ^= (uint8_t)(0x80u >> (bit % 8u));
}

/* The CRC of the frame as it stands, its bytes before `from` known clean. */
static uint16_t crc_from(unsigned from)
{
    uint16_t crc = g_prefix[from];
    unsigned i;

    for (i = from; i < FRAME_LEN; i++) {
        crc = tfw_crc16_update(crc, g_frame[i]);
    }
    return crc;
}

static void expect_detected(unsigned first_bit, const char *what, unsigned a, unsigned b)
{
    const uint16_t dirty = crc_from(first_bit / 8u);

    if (dirty == g_clean) {
        printf("seed 0x%08lx: %s %u/%u undetected (crc 0x%04x)\n", (unsigned long)SEED, what, a, b,
               g_clean);
        TEST_FAIL_MESSAGE("an error was not detected");
    }
}

void setUp(void)
{
    unsigned i;

    rng     = SEED;
    g_frame = (uint8_t *)malloc(FRAME_LEN);
    TEST_ASSERT_NOT_NULL(g_frame);
    for (i = 0u; i < FRAME_LEN; i++) {
        g_frame[i] = (uint8_t)rnd();
    }
    g_prefix[0] = TFW_CRC16_INIT;
    for (i = 0u; i < FRAME_LEN; i++) {
        g_prefix[i + 1u] = tfw_crc16_update(g_prefix[i], g_frame[i]);
    }
    g_clean = g_prefix[FRAME_LEN];
    TEST_ASSERT_EQUAL_HEX16(g_clean, tfw_crc16(g_frame, (uint16_t)FRAME_LEN));
}

void tearDown(void)
{
    /* every case restores the frame: its CRC is back */
    TEST_ASSERT_EQUAL_HEX16(g_clean, tfw_crc16(g_frame, (uint16_t)FRAME_LEN));
    free(g_frame);
}

static void test_crc16_mutate_known_vectors_still_match(void)
{
    uint8_t *p = (uint8_t *)malloc(TFWV_CRC_CHECK_LEN);

    TEST_ASSERT_NOT_NULL(p);
    memcpy(p, TFWV_CRC_CHECK_INPUT, TFWV_CRC_CHECK_LEN);
    TEST_ASSERT_EQUAL_HEX16(TFWV_CRC_CHECK_RESULT, tfw_crc16(p, (uint16_t)TFWV_CRC_CHECK_LEN));
    memcpy(p, TFWV_CRC_ORDER_A, 2u);
    TEST_ASSERT_EQUAL_HEX16(TFWV_CRC_ORDER_A_RESULT, tfw_crc16(p, 2u));
    memcpy(p, TFWV_CRC_ORDER_B, 2u);
    TEST_ASSERT_EQUAL_HEX16(TFWV_CRC_ORDER_B_RESULT, tfw_crc16(p, 2u));
    TEST_ASSERT_EQUAL_HEX16(TFWV_CRC_EMPTY_RESULT, tfw_crc16(p, 0u));
    free(p);
}

static void test_crc16_mutate_every_two_bit_error_is_detected(void)
{
    unsigned a, b;

    for (a = 0u; a < NBITS; a++) {
        flip(g_frame, a);
        for (b = a + 1u; b < NBITS; b++) {
            flip(g_frame, b);
            expect_detected(a, "bits", a, b);
            flip(g_frame, b);
        }
        flip(g_frame, a);
    }
}

/* A burst of length len at bit `at`: its first and last bits set, the interior
 * from `inner` (bit k of inner is burst bit k + 1). */
static void burst(unsigned at, unsigned len, uint32_t inner)
{
    unsigned k;

    flip(g_frame, at);
    for (k = 1u; k + 1u < len; k++) {
        if (inner & (1ul << (k - 1u))) {
            flip(g_frame, at + k);
        }
    }
    if (len > 1u) {
        flip(g_frame, at + len - 1u);
    }
}

/* Every burst length 1..16 at every position, with an all-ones, an all-zeros,
 * an alternating and two seeded interiors. Length 1 is every single-bit
 * error. */
static void test_crc16_mutate_every_burst_up_to_16_bits_is_detected(void)
{
    unsigned len, at, v;

    for (len = 1u; len <= 16u; len++) {
        for (at = 0u; at + len <= NBITS; at++) {
            const uint32_t inners[5] = { 0x3FFFu, 0u, 0x1555u, rnd(), rnd() };

            for (v = 0u; v < 5u; v++) {
                burst(at, len, inners[v]);
                expect_detected(at, "burst at/len", at, len);
                burst(at, len, inners[v]);
            }
        }
    }
}

/* Every interior of every burst length 3..16, at the frame's first bit, a byte
 * boundary straddle in the middle, and flush with its last bit. */
static void test_crc16_mutate_every_burst_pattern_is_detected_at_the_edges(void)
{
    unsigned len, p;
    uint32_t inner;

    for (len = 3u; len <= 16u; len++) {
        const unsigned at[3] = { 0u, NBITS / 2u - 5u, NBITS - len };

        for (p = 0u; p < 3u; p++) {
            for (inner = 0u; inner < (1ul << (len - 2u)); inner++) {
                burst(at[p], len, inner);
                expect_detected(at[p], "burst at/len", at[p], len);
                burst(at[p], len, inner);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_mutate_known_vectors_still_match);
    RUN_TEST(test_crc16_mutate_every_two_bit_error_is_detected);
    RUN_TEST(test_crc16_mutate_every_burst_up_to_16_bits_is_detected);
    RUN_TEST(test_crc16_mutate_every_burst_pattern_is_detected_at_the_edges);
    return UNITY_END();
}
