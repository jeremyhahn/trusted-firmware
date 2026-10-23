/*
 * Seeded mutation of the claims set encoder (tfw_uccs_encode, src/caps/uccs.c)
 * on test_uccs's link. Its inputs are claim values, not wire bytes: every
 * nonce_len from 0 to 255, every bit of the nonce, the DeviceID and the
 * firmware hash, all 0x00, all 0xFF and random values, each in an array of
 * exactly its width, through writers of every capacity from 0 to past the
 * whole token with up to three bytes already in them and guard bytes behind.
 *
 * The oracle is test_uccs's vector with the three values put where it carries
 * them: a nonce_len other than TFW_ALIAS_NONCE_LEN is TFW_ERR_RANGE and writes
 * nothing, a writer without room for TFW_UCCS_LEN bytes is TFW_ERR_TOO_LONG,
 * and otherwise the token is the vector's bytes with the values substituted.
 * Nothing is ever written past the capacity. Afterwards the clean values give
 * the vector.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "uccs.h"
#include "uccs_vectors.h"
#include "vectors.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"

static const uint8_t NONCE[]  = TFWV_UCCS_NONCE;
static const uint8_t UEID[]   = TFWV_UCCS_UEID;
static const uint8_t FWHASH[] = TFWV_UCCS_FWHASH;
static const uint8_t WANT[]   = TFWV_UCCS;

#define GUARD_LEN    16u
#define GUARD        0xA5u
#define PREFIX       0x5Au
#define RANDOM_RUNS  3000u

/* Where the vector carries each value: the nonce after the map head, its key
 * and its two-byte bstr head; the DeviceID after the UEID's type byte. */
#define AT_NONCE  4u
#define AT_UEID   (TFW_UCCS_OFF_UEID + 1u)

static uint32_t rng = 0x0CC5F1A7u;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint8_t  buf[TFW_UCCS_LEN + 3u + GUARD_LEN];
static unsigned at_hash;
static unsigned encoded_runs;

/* The values under test, each in an array of exactly its width so a read past
 * one is a read past the array. */
static uint8_t nonce[TFW_ALIAS_NONCE_LEN];
static uint8_t ueid[TFW_ATTEST_ID_LEN];
static uint8_t fwhash[TFW_SHA256_LEN];

static void clean_values(void)
{
    memcpy(nonce, NONCE, sizeof nonce);
    memcpy(ueid, UEID, sizeof ueid);
    memcpy(fwhash, FWHASH, sizeof fwhash);
}

void setUp(void)
{
    unsigned i;

    clean_values();
    encoded_runs = 0u;
    /* The firmware hash sits in the CoSWID; find it in the vector rather than
     * restating the CoSWID's layout here. */
    at_hash = 0u;
    for (i = 0u; at_hash == 0u && i + sizeof fwhash <= TFW_UCCS_LEN; i++) {
        unsigned k = 0u;

        while (k < sizeof fwhash && WANT[i + k] == FWHASH[k]) {
            k++;
        }
        if (k == sizeof fwhash) {
            at_hash = i;
        }
    }
    TEST_ASSERT_TRUE(at_hash > AT_UEID);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NONCE, &WANT[AT_NONCE], sizeof nonce);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(UEID, &WANT[AT_UEID], sizeof ueid);
}

void tearDown(void) {}

/* One call against the oracle: the given nonce_len, a writer of capacity cap
 * holding pre bytes, the values as they stand. */
static void check(uint8_t nonce_len, uint8_t cap, uint8_t pre)
{
    uint8_t      want[TFW_UCCS_LEN];
    tfw_writer_t w;
    tfw_status_t st;
    unsigned     i;

    memset(buf, GUARD, sizeof buf);
    tfw_writer_init(&w, buf, cap);
    for (i = 0u; i < pre; i++) {
        tfw_write_u8(&w, PREFIX);
    }
    st = tfw_uccs_encode(&w, nonce, nonce_len, ueid, fwhash);
    for (i = cap; i < sizeof buf; i++) {
        if (buf[i] != GUARD) {
            TEST_FAIL_MESSAGE("a byte was written past the writer's capacity");
        }
    }
    if (nonce_len != TFW_ALIAS_NONCE_LEN) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, st);
        for (i = (pre < cap) ? pre : cap; i < sizeof buf; i++) {
            if (buf[i] != GUARD) {
                TEST_FAIL_MESSAGE("a refused nonce wrote into the writer");
            }
        }
        return;
    }
    if ((unsigned)pre + TFW_UCCS_LEN > cap) {
        TEST_ASSERT_EQUAL(TFW_ERR_TOO_LONG, st);
        return;
    }
    TEST_ASSERT_EQUAL(TFW_OK, st);
    TEST_ASSERT_EQUAL_UINT8(pre + TFW_UCCS_LEN, tfw_writer_len(&w));
    memcpy(want, WANT, sizeof want);
    memcpy(&want[AT_NONCE], nonce, sizeof nonce);
    memcpy(&want[AT_UEID], ueid, sizeof ueid);
    memcpy(&want[at_hash], fwhash, sizeof fwhash);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, &buf[pre], TFW_UCCS_LEN);
    encoded_runs++;
}

static void clean_afterwards(void)
{
    tfw_writer_t w;

    clean_values();
    memset(buf, GUARD, sizeof buf);
    tfw_writer_init(&w, buf, TFW_UCCS_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_uccs_encode(&w, nonce, TFW_ALIAS_NONCE_LEN, ueid, fwhash));
    TEST_ASSERT_EQUAL_UINT8(TFWV_UCCS_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, TFWV_UCCS_LEN);
    TEST_ASSERT_EQUAL_HEX8(GUARD, buf[TFW_UCCS_LEN]);
}

/* ------------------------------------------------------------- the tests */

/* Every nonce_len, 0, 1, the width, one past it and 255 among them, through a
 * writer with room and one without. */
static void test_every_nonce_length(void)
{
    unsigned n, runs = 0u;

    for (n = 0u; n < 256u; n++) {
        check((uint8_t)n, TFW_UCCS_LEN, 0u);
        check((uint8_t)n, 1u, 0u);
        check((uint8_t)n, 0u, 0u);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(256u, runs);
    TEST_ASSERT_EQUAL_UINT(1u, encoded_runs);
    clean_afterwards();
}

/* Every writer capacity from 0 to past the whole token, with up to three bytes
 * already in it: it fits exactly when the room left is TFW_UCCS_LEN. */
static void test_every_writer_capacity(void)
{
    unsigned cap, pre, runs = 0u;

    for (cap = 0u; cap <= TFW_UCCS_LEN + 3u; cap++) {
        for (pre = 0u; pre < 4u; pre++) {
            check(TFW_ALIAS_NONCE_LEN, (uint8_t)cap, (uint8_t)pre);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT((TFW_UCCS_LEN + 4u) * 4u, runs);
    TEST_ASSERT_EQUAL_UINT(1u + 2u + 3u + 4u, encoded_runs);
    clean_afterwards();
}

/* Every bit of every value. Each still encodes, and the bit lands where the
 * vector carries the value and nowhere else. */
static void test_every_bit_flip_of_every_value(void)
{
    uint8_t *const field[3] = { nonce, ueid, fwhash };
    const unsigned width[3] = { sizeof nonce, sizeof ueid, sizeof fwhash };
    unsigned       f, bit, runs = 0u;

    for (f = 0u; f < 3u; f++) {
        for (bit = 0u; bit < width[f] * 8u; bit++) {
            clean_values();
            field[f][bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            check(TFW_ALIAS_NONCE_LEN, TFW_UCCS_LEN, 0u);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT((sizeof nonce + sizeof ueid + sizeof fwhash) * 8u, runs);
    TEST_ASSERT_EQUAL_UINT(runs, encoded_runs);
    clean_afterwards();
}

/* All 0x00 and all 0xFF in every value, through every capacity. */
static void test_all_zero_and_all_ff(void)
{
    unsigned fill, cap, runs = 0u;

    for (fill = 0u; fill < 2u; fill++) {
        memset(nonce, fill ? 0xFF : 0x00, sizeof nonce);
        memset(ueid, fill ? 0xFF : 0x00, sizeof ueid);
        memset(fwhash, fill ? 0xFF : 0x00, sizeof fwhash);
        for (cap = 0u; cap <= TFW_UCCS_LEN; cap++) {
            check(TFW_ALIAS_NONCE_LEN, (uint8_t)cap, 0u);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * (TFW_UCCS_LEN + 1u), runs);
    TEST_ASSERT_EQUAL_UINT(2u, encoded_runs);
    clean_afterwards();
}

/* Random values, a random nonce_len biased toward the width, a random
 * capacity biased toward the edge and a random prefix. */
static void test_random_values(void)
{
    unsigned i, k, runs = 0u;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        uint32_t x = xorshift32();
        uint8_t  n = ((x & 3u) != 0u) ? (uint8_t)TFW_ALIAS_NONCE_LEN : (uint8_t)(x >> 8);
        uint8_t  pre = (uint8_t)((x >> 2) & 3u);
        uint8_t  cap;

        if (((x >> 4) & 1u) != 0u) {
            cap = (uint8_t)(TFW_UCCS_LEN - 2u + ((x >> 16) % 6u));
        } else {
            cap = (uint8_t)((x >> 16) % (TFW_UCCS_LEN + 4u));
        }
        for (k = 0u; k < sizeof nonce; k++) {
            nonce[k] = (uint8_t)xorshift32();
        }
        for (k = 0u; k < sizeof ueid; k++) {
            ueid[k] = (uint8_t)xorshift32();
        }
        for (k = 0u; k < sizeof fwhash; k++) {
            fwhash[k] = (uint8_t)xorshift32();
        }
        check(n, cap, pre);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, runs);
    TEST_ASSERT_TRUE(encoded_runs > 0u);
    clean_afterwards();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_nonce_length);
    RUN_TEST(test_every_writer_capacity);
    RUN_TEST(test_every_bit_flip_of_every_value);
    RUN_TEST(test_all_zero_and_all_ff);
    RUN_TEST(test_random_values);
    return UNITY_END();
}
