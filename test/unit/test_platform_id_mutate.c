/* The platform id under mutation (issue #163): src/core/platform_id.c read
 * over flipped, torn, truncated and random platform records.
 *
 * test_appstate_mutate.c pins that a fresh read gives zero or exactly the
 * record's bytes. This suite pins what that leaves open: a refusal AFTER a good
 * read clears the serial the good read left (no stale id), a record torn
 * between two writes reads as one of them whole or as nothing (no partial id),
 * and a failed EEPROM read is a refusal too. Refused means what platform_id.c
 * defines: an all-zero serial and an all-zero operator key.
 *
 * Deterministic: random records come from xorshift32 seeded with a literal,
 * and a failure prints the seed and the case. Every record is a heap buffer of
 * exactly TFW_BOOT_EE_BLOCK_LEN.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "boot_platform.h"
#include "platform_id.h"

#define SEED        0x9147F00Du
#define RANDOM_RUNS 4000u

static uint32_t rng;
static uint8_t *rec_a, *rec_b, *mix;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int all_zero(const uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        if (p[i] != 0u) { return 0; }
    }
    return 1;
}

/* A valid record whose serial and operator key are filled from fill. */
static void make_record(uint8_t *raw, uint8_t fill)
{
    tfw_boot_platform_t p;
    unsigned            i;

    memset(&p, 0, sizeof p);
    memset(raw, 0, TFW_BOOT_EE_BLOCK_LEN);
    for (i = 0u; i < TFW_BOOT_SERIAL_LEN; i++) { p.serial[i] = (uint8_t)(fill + i); }
    for (i = 0u; i < TFW_BOOT_OPERATOR_PUBKEY_LEN; i++) { p.operator_pubkey[i] = (uint8_t)(fill ^ (0x80u + i)); }
    p.rollback_floor = 3u;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    tfw_boot_platform_serialise(&p, raw);
}

static void load(const uint8_t *raw)
{
    memcpy(tfw_mock_eeprom() + TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_BLOCK_LEN);
    tfw_platform_id_init();
}

/* What the module answers for raw, read straight after a good record. */
static int read_after_good(const uint8_t *raw, uint8_t key[TFW_BOOT_OPERATOR_PUBKEY_LEN])
{
    load(rec_a);
    TEST_ASSERT_EQUAL_MEMORY(&rec_a[TFW_BOOT_EE_O_SERIAL], tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN);
    load(raw);
    memset(key, 0xA5, TFW_BOOT_OPERATOR_PUBKEY_LEN);
    tfw_platform_id_operator_pubkey(key);
    return all_zero(tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN) && all_zero(key, TFW_BOOT_OPERATOR_PUBKEY_LEN);
}

/* raw is refused, or read as exactly its own serial and key. */
static void expect_whole(const uint8_t *raw, const char *what, unsigned a)
{
    uint8_t   key[TFW_BOOT_OPERATOR_PUBKEY_LEN];
    const int refused = read_after_good(raw, key);
    tfw_boot_platform_t p;
    const int parses = tfw_boot_platform_parse(raw, &p) == TFW_OK;

    if (parses ? (memcmp(tfw_platform_id_serial(), &raw[TFW_BOOT_EE_O_SERIAL], TFW_BOOT_SERIAL_LEN) != 0
                  || memcmp(key, &raw[TFW_BOOT_EE_O_OPERATOR_PUBKEY], sizeof key) != 0)
               : !refused) {
        printf("seed 0x%08lx: %s %u: %s\n", (unsigned long)SEED, what, a,
               parses ? "an accepted record was not read whole" : "a refused record left an id");
        TEST_FAIL_MESSAGE("a partial or stale platform id");
    }
}

void setUp(void)
{
    tfw_mock_reset();
    rec_a = (uint8_t *)malloc(TFW_BOOT_EE_BLOCK_LEN);
    rec_b = (uint8_t *)malloc(TFW_BOOT_EE_BLOCK_LEN);
    mix   = (uint8_t *)malloc(TFW_BOOT_EE_BLOCK_LEN);
    TEST_ASSERT_NOT_NULL(rec_a);
    TEST_ASSERT_NOT_NULL(rec_b);
    TEST_ASSERT_NOT_NULL(mix);
    make_record(rec_a, 0x10u);
    make_record(rec_b, 0xC0u);
}

void tearDown(void)
{
    free(rec_a);
    free(rec_b);
    free(mix);
}

/* ------------------------------------------------------------------ cases */

static void test_platform_id_mutate_every_bit_flip_after_a_good_read_clears_the_id(void)
{
    uint8_t  key[TFW_BOOT_OPERATOR_PUBKEY_LEN];
    unsigned bit;

    for (bit = 0u; bit < TFW_BOOT_EE_BLOCK_LEN * 8u; bit++) {
        memcpy(mix, rec_b, TFW_BOOT_EE_BLOCK_LEN);
        mix[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        if (!read_after_good(mix, key)) {
            /* A flip outside the CRC's span and the checked header still
             * parses; anything else must be refused. */
            expect_whole(mix, "bit", bit);
        }
    }
}

/* B written over A, torn after k bytes; and B cut at k, the rest erased or
 * zero. Each reads as A, as B, or as nothing -- never a mixture. */
static void test_platform_id_mutate_a_torn_or_cut_record_is_one_whole_record_or_none(void)
{
    static const uint8_t tails[2] = { 0xFFu, 0x00u };
    uint8_t  key[TFW_BOOT_OPERATOR_PUBKEY_LEN];
    unsigned k, t;

    for (k = 0u; k <= TFW_BOOT_EE_BLOCK_LEN; k++) {
        memcpy(mix, rec_b, k);
        memcpy(&mix[k], &rec_a[k], TFW_BOOT_EE_BLOCK_LEN - k);
        expect_whole(mix, "torn", k);
        if (memcmp(mix, rec_a, TFW_BOOT_EE_BLOCK_LEN) != 0 && memcmp(mix, rec_b, TFW_BOOT_EE_BLOCK_LEN) != 0) {
            if (!read_after_good(mix, key)) {
                printf("seed 0x%08lx: torn %u read as a record\n", (unsigned long)SEED, k);
                TEST_FAIL_MESSAGE("a record torn between two writes was read");
            }
        }
        for (t = 0u; t < 2u; t++) {
            memcpy(mix, rec_b, k);
            memset(&mix[k], tails[t], TFW_BOOT_EE_BLOCK_LEN - k);
            if (k < TFW_BOOT_EE_BLOCK_LEN && !read_after_good(mix, key)) {
                printf("seed 0x%08lx: cut %u tail 0x%02x read as a record\n", (unsigned long)SEED, k,
                       tails[t]);
                TEST_FAIL_MESSAGE("a cut record was read");
            }
        }
    }
}

static void test_platform_id_mutate_a_failed_eeprom_read_after_a_good_one_clears_the_id(void)
{
    uint8_t key[TFW_BOOT_OPERATOR_PUBKEY_LEN];

    load(rec_a);
    TEST_ASSERT_FALSE(all_zero(tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN));
    tfw_mock_eeprom_read_fails(1u);
    tfw_platform_id_init();
    memset(key, 0xA5, sizeof key);
    tfw_platform_id_operator_pubkey(key);
    TEST_ASSERT_TRUE(all_zero(tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN));
    TEST_ASSERT_TRUE(all_zero(key, sizeof key));
    tfw_mock_eeprom_read_fails(0u);
    load(rec_b);
    TEST_ASSERT_EQUAL_MEMORY(&rec_b[TFW_BOOT_EE_O_SERIAL], tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN);
}

/* Random bytes, half of them given the right magic, layout and CRC: a valid
 * one is read whole, the rest are refused. */
static void test_platform_id_mutate_random_records_read_whole_or_not_at_all(void)
{
    unsigned run, i;

    rng = SEED;
    for (run = 0u; run < RANDOM_RUNS; run++) {
        for (i = 0u; i < TFW_BOOT_EE_BLOCK_LEN; i++) {
            mix[i] = (uint8_t)rnd();
        }
        if (run & 1u) {
            tfw_boot_wr_u32(&mix[TFW_BOOT_EE_O_MAGIC], TFW_BOOT_EE_MAGIC);
            mix[TFW_BOOT_EE_O_LAYOUT] = TFW_BOOT_EE_VERSION;
            if (rnd() % 4u) {
                tfw_boot_wr_u16(&mix[TFW_BOOT_EE_O_CRC], tfw_boot_platform_crc(mix));
            }
        }
        expect_whole(mix, "run", run);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_platform_id_mutate_every_bit_flip_after_a_good_read_clears_the_id);
    RUN_TEST(test_platform_id_mutate_a_torn_or_cut_record_is_one_whole_record_or_none);
    RUN_TEST(test_platform_id_mutate_a_failed_eeprom_read_after_a_good_one_clears_the_id);
    RUN_TEST(test_platform_id_mutate_random_records_read_whole_or_not_at_all);
    return UNITY_END();
}
