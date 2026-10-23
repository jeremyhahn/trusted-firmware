/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The P-256 DeviceID derivation (src/crypto/deviceid_p256.c) under mutated
 * seeds, linked as test_deviceid_p256 is:
 *   - every one-bit change of a seed changes the scalar and the public key;
 *   - the same seed gives the same scalar and key, call after call;
 *   - no seed gives a scalar outside [1, n-1] or a key off the curve: every
 *     byte at every value of each base seed for the scalar, and every bit
 *     flip plus each byte at 0x00 and 0xFF for the key, which is checked on
 *     the curve and equal to the point of its scalar.
 * The bases are the edges the derivation hashes again (zero, n, n+1, all
 * ones, n-1, one) and seeds drawn from a fixed-seed xorshift, printed with
 * any failure. The seeds 1 and n-1 are scalars micro-ecc refuses: check_key
 * asserts they are hashed again and still give a key.
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "uECC.h"
#include "sha256.h"
#include "deviceid_p256.h"

/* test_deviceid_p256 links without src/core/secure_zero.c; this one only
 * zeroes. */
void tfw_secure_zero(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    size_t            i;

    for (i = 0; i < n; i++) { v[i] = 0u; }
}

void setUp(void) {}
void tearDown(void) {}

#define RNG_SEED 0x163D1Du
#define NBASES   10u
#define NDRAWN   4u

static const uint8_t P256_N[32] = {
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51
};

static uint8_t  bases[NBASES][32];
static uint32_t rng = RNG_SEED;
static char     why[128];

static uint32_t xorshift(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void add_small(uint8_t x[32], int delta)
{
    int i;
    int c = delta;

    for (i = 31; i >= 0 && c != 0; i--) {
        const int v = (int)x[i] + c;
        x[i] = (uint8_t)(v & 0xFF);
        c = v < 0 ? -1 : (v > 0xFF ? 1 : 0);
    }
}

static void make_bases(void)
{
    unsigned b;
    unsigned i;

    memset(bases[0], 0x00, 32);                          /* zero */
    memcpy(bases[1], P256_N, 32);                        /* n */
    memcpy(bases[2], P256_N, 32); add_small(bases[2], 1);  /* n+1 */
    memset(bases[3], 0xFF, 32);                          /* all ones */
    memcpy(bases[4], P256_N, 32); add_small(bases[4], -1); /* n-1 */
    memset(bases[5], 0x00, 32); bases[5][31] = 1u;       /* one */
    rng = RNG_SEED;
    for (b = NBASES - NDRAWN; b < NBASES; b++) {
        for (i = 0u; i < 32u; i++) { bases[b][i] = (uint8_t)xorshift(); }
    }
}

static int cmp_be(const uint8_t a[32], const uint8_t b[32])
{
    return memcmp(a, b, 32);
}

static int all_zero(const uint8_t x[32])
{
    unsigned i;
    uint8_t  acc = 0u;

    for (i = 0u; i < 32u; i++) { acc |= x[i]; }
    return acc == 0u;
}

static void name(unsigned b, const char *what, unsigned i, unsigned v)
{
    snprintf(why, sizeof why, "rng seed 0x%X, base %u, %s %u -> 0x%02X",
             (unsigned)RNG_SEED, b, what, i, v);
}

static void check_scalar(const uint8_t seed[32], uint8_t d[32])
{
    TEST_ASSERT_TRUE_MESSAGE(tfw_deviceid_p256_scalar(seed, d), why);
    TEST_ASSERT_FALSE_MESSAGE(all_zero(d), why);
    TEST_ASSERT_TRUE_MESSAGE(cmp_be(d, P256_N) < 0, why);
}

/* The key on the curve and the point of its own scalar. */
static int check_key(const uint8_t seed[32], uint8_t d[32], uint8_t pub[64])
{
    uint8_t want[64];
    uint8_t n_less_1[32];
    uint8_t one[32];

    check_scalar(seed, d);
    memcpy(n_less_1, P256_N, 32); add_small(n_less_1, -1);
    memset(one, 0x00, 32); one[31] = 1u;
    /* micro-ecc refuses the scalars 1 and n-1, so the derivation never keeps
     * them: the seeds 1 and n-1 are hashed again. */
    TEST_ASSERT_TRUE_MESSAGE(memcmp(d, one, 32) != 0, why);
    TEST_ASSERT_TRUE_MESSAGE(memcmp(d, n_less_1, 32) != 0, why);
    memset(pub, 0xA5, 64);
    TEST_ASSERT_TRUE_MESSAGE(tfw_deviceid_p256_public(seed, pub), why);
    TEST_ASSERT_TRUE_MESSAGE(uECC_valid_public_key(pub, uECC_secp256r1()), why);
    TEST_ASSERT_TRUE_MESSAGE(uECC_compute_public_key(d, want, uECC_secp256r1()), why);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, pub, 64, why);
    return 1;
}

static void test_every_seed_byte_at_every_value_gives_a_scalar_in_range(void)
{
    uint8_t  seed[32];
    uint8_t  d[32];
    unsigned b;
    unsigned i;
    unsigned v;

    make_bases();
    for (b = 0u; b < NBASES; b++) {
        for (i = 0u; i < 32u; i++) {
            for (v = 0u; v < 256u; v++) {
                memcpy(seed, bases[b], sizeof seed);
                seed[i] = (uint8_t)v;
                name(b, "byte", i, v);
                check_scalar(seed, d);
            }
        }
    }
}

static void test_a_one_bit_change_changes_the_key_and_stays_on_the_curve(void)
{
    uint8_t  seed[32];
    uint8_t  d0[32];
    uint8_t  p0[64];
    uint8_t  d[32];
    uint8_t  p[64];
    unsigned b;
    unsigned bit;
    int      have0;

    make_bases();
    for (b = 0u; b < NBASES; b++) {
        name(b, "unmutated", 0u, bases[b][0]);
        have0 = check_key(bases[b], d0, p0);
        for (bit = 0u; bit < 256u; bit++) {
            memcpy(seed, bases[b], sizeof seed);
            seed[bit / 8u] ^= (uint8_t)(0x80u >> (bit % 8u));
            name(b, "bit", bit, seed[bit / 8u]);
            if (check_key(seed, d, p) && have0) {
                TEST_ASSERT_TRUE_MESSAGE(memcmp(p, p0, 64) != 0, why);
            }
            TEST_ASSERT_TRUE_MESSAGE(memcmp(d, d0, 32) != 0, why);
        }
    }
}

static void test_each_byte_at_its_extremes_stays_on_the_curve(void)
{
    static const uint8_t extremes[] = { 0x00u, 0xFFu };
    uint8_t  seed[32];
    uint8_t  d[32];
    uint8_t  p[64];
    unsigned b;
    unsigned i;
    unsigned k;

    make_bases();
    for (b = 0u; b < NBASES; b++) {
        for (i = 0u; i < 32u; i++) {
            for (k = 0u; k < sizeof extremes; k++) {
                if (bases[b][i] == extremes[k]) { continue; }
                memcpy(seed, bases[b], sizeof seed);
                seed[i] = extremes[k];
                name(b, "byte", i, extremes[k]);
                check_key(seed, d, p);
            }
        }
    }
}

static void test_the_same_seed_gives_the_same_key(void)
{
    uint8_t  d1[32];
    uint8_t  d2[32];
    uint8_t  p1[64];
    uint8_t  p2[64];
    unsigned b;
    unsigned r;
    int      have;

    make_bases();
    for (b = 0u; b < NBASES; b++) {
        name(b, "repeat", 0u, bases[b][0]);
        have = check_key(bases[b], d1, p1);
        for (r = 0u; r < 3u; r++) {
            (void)check_key(bases[b], d2, p2);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(d1, d2, 32, why);
            if (have) { TEST_ASSERT_EQUAL_MEMORY_MESSAGE(p1, p2, 64, why); }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_seed_byte_at_every_value_gives_a_scalar_in_range);
    RUN_TEST(test_a_one_bit_change_changes_the_key_and_stays_on_the_curve);
    RUN_TEST(test_each_byte_at_its_extremes_stays_on_the_curve);
    RUN_TEST(test_the_same_seed_gives_the_same_key);
    return UNITY_END();
}
