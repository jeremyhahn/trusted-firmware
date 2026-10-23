/* Secure zero under mutation (issue #163): src/core/secure_zero.c asked to
 * clear every length 0..300 at every alignment 0..7, and at seeded random
 * offsets and lengths, inside a guarded arena of seeded random non-zero bytes.
 *
 * What is pinned: exactly the range asked for is zero afterwards and every
 * byte outside it -- before, after, and the guard bands either side -- is the
 * byte it was. And a buffer of exactly n bytes on the heap is cleared without
 * a single access past it, which ASan sees in the sanitised build.
 * test_secure_zero.c pins the plain call; this suite pins the edges.
 *
 * Deterministic: xorshift32 seeded with a literal; a failure prints the seed,
 * the length, the alignment and the first wrong byte.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "secure_zero.h"

#define SEED        0x5EC0E0A1u
#define MAX_LEN     300u
#define ALIGNS      8u
#define GUARD       32u
#define ARENA       (GUARD + ALIGNS + MAX_LEN + GUARD)
#define RANDOM_RUNS 20000u

static uint32_t rng;
static uint8_t *arena, *orig;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    arena = (uint8_t *)malloc(ARENA);
    orig  = (uint8_t *)malloc(ARENA);
    TEST_ASSERT_NOT_NULL(arena);
    TEST_ASSERT_NOT_NULL(orig);
}

void tearDown(void)
{
    free(arena);
    free(orig);
}

/* Fill the arena with non-zero bytes, so a stray zero anywhere shows. */
static void fill(void)
{
    unsigned i;
    for (i = 0u; i < ARENA; i++) {
        arena[i] = (uint8_t)(rnd() | 1u);
    }
    memcpy(orig, arena, ARENA);
}

/* Clear [start, start + n) and check exactly that range changed, to zero. */
static void clear_and_check(unsigned start, unsigned n, unsigned align)
{
    unsigned i;

    fill();
    tfw_secure_zero(&arena[start], n);
    for (i = 0u; i < ARENA; i++) {
        const int inside = i >= start && i < start + n;
        if (inside ? arena[i] != 0u : arena[i] != orig[i]) {
            printf("seed 0x%08lx: len %u align %u start %u: byte %u is 0x%02x\n", (unsigned long)SEED, n,
                   align, start, i, arena[i]);
            TEST_FAIL_MESSAGE(inside ? "a byte in the range was not cleared" : "a byte outside the range changed");
        }
    }
}

/* ------------------------------------------------------------------ cases */

static void test_secure_zero_mutate_every_length_at_every_alignment_clears_exactly_its_range(void)
{
    unsigned n, a;

    rng = SEED;
    for (n = 0u; n <= MAX_LEN; n++) {
        for (a = 0u; a < ALIGNS; a++) {
            clear_and_check(GUARD + a, n, a);
        }
    }
}

/* A heap buffer of exactly n bytes, at the alignment malloc gives and at
 * every byte offset into a block of n + a: no access past its end. */
static void test_secure_zero_mutate_an_exact_heap_buffer_is_cleared_without_an_overrun(void)
{
    unsigned n, a, i;

    rng = SEED ^ 0xFFFFFFFFu;
    for (n = 1u; n <= MAX_LEN; n++) {
        for (a = 0u; a < ALIGNS; a++) {
            uint8_t *blk = (uint8_t *)malloc(n + a);
            TEST_ASSERT_NOT_NULL(blk);
            for (i = 0u; i < n + a; i++) { blk[i] = (uint8_t)(rnd() | 1u); }
            tfw_secure_zero(&blk[a], n);
            for (i = 0u; i < n + a; i++) {
                if ((i < a) ? blk[i] == 0u : blk[i] != 0u) {
                    printf("seed 0x%08lx: len %u align %u: byte %u is 0x%02x\n", (unsigned long)SEED, n, a, i,
                           blk[i]);
                    TEST_FAIL_MESSAGE("an exact buffer was not cleared to its edges only");
                }
            }
            free(blk);
        }
    }
}

/* Clearing a range that is already zero, or clearing twice, changes nothing
 * outside it either. */
static void test_secure_zero_mutate_clearing_twice_is_the_same_as_once(void)
{
    unsigned n, a, i;

    rng = SEED ^ 0x0F0F0F0Fu;
    for (n = 0u; n <= MAX_LEN; n += 7u) {
        for (a = 0u; a < ALIGNS; a++) {
            clear_and_check(GUARD + a, n, a);
            tfw_secure_zero(&arena[GUARD + a], n);
            for (i = 0u; i < ARENA; i++) {
                const int inside = i >= GUARD + a && i < GUARD + a + n;
                if (inside ? arena[i] != 0u : arena[i] != orig[i]) {
                    printf("seed 0x%08lx: len %u align %u: second clear, byte %u\n", (unsigned long)SEED, n, a,
                           i);
                    TEST_FAIL_MESSAGE("a second clear changed a byte");
                }
            }
        }
    }
}

static void test_secure_zero_mutate_random_ranges_clear_exactly_their_range(void)
{
    unsigned run;

    rng = SEED ^ 0xA5A5A5A5u;
    for (run = 0u; run < RANDOM_RUNS; run++) {
        const unsigned start = rnd() % (ARENA + 1u);
        const unsigned n     = rnd() % (ARENA - start + 1u);
        clear_and_check(start, n, start % ALIGNS);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_secure_zero_mutate_every_length_at_every_alignment_clears_exactly_its_range);
    RUN_TEST(test_secure_zero_mutate_an_exact_heap_buffer_is_cleared_without_an_overrun);
    RUN_TEST(test_secure_zero_mutate_clearing_twice_is_the_same_as_once);
    RUN_TEST(test_secure_zero_mutate_random_ranges_clear_exactly_their_range);
    return UNITY_END();
}
