/*
 * Seeded mutation of the platform block the application's trial reads at
 * start-up (src/core/ota_trial.c, #77), on test_ota_trial's link: bit flips,
 * all 0x00, all 0xFF, random bytes, every layout, state and fail-count byte,
 * and hostile fields under a valid CRC. Each record is run through what
 * test_ota_trial runs -- tfw_ota_trial_start, then tfw_ota_trial_poll to one
 * millisecond short of the deadline and to it.
 *
 * The oracle is the layout in include/tfw/boot.h, written out here as bytes:
 * the trial arms if and only if the magic, the layout byte and the CRC are
 * right and boot_state is TRIAL. Whatever the record, the application never
 * writes it back: settling a trial, confirming it and counting a failure are
 * tfw-boot's, so a hostile record can neither confirm an image nor move the
 * fail count anywhere, least of all round to healthy.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_platform.h"
#include "ota_trial.h"

#define DEADLINE_MS ((tfw_ms_t)TFW_OTA_TRIAL_TIMEOUT_S * 1000u)
#define RANDOM_RUNS 3000u
#define HOSTILE_RUNS 3000u

static uint32_t rng = 0x7A31C0DEu;
static unsigned armed_runs, idle_runs;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    tfw_mock_reset();
    armed_runs = idle_runs = 0u;
}
void tearDown(void) { }

static void seal(uint8_t *raw)
{
    uint16_t crc = tfw_boot_platform_crc(raw);

    raw[TFW_BOOT_EE_O_CRC]      = (uint8_t)crc;
    raw[TFW_BOOT_EE_O_CRC + 1u] = (uint8_t)(crc >> 8);
}

/* A TRIAL record with every field the application ignores set to something. */
static void clean(uint8_t *raw, uint8_t boot_state)
{
    tfw_boot_platform_t p;
    uint16_t            i;

    memset(&p, 0, sizeof p);
    memset(raw, 0, TFW_BOOT_EE_BLOCK_LEN);
    p.boot_state = boot_state;
    tfw_boot_platform_serialise(&p, raw);
    for (i = TFW_BOOT_EE_O_PUBKEY_HASH; i < TFW_BOOT_EE_O_BOOT_STATE; i++) {
        raw[i] = (uint8_t)(0x30u + i);
    }
    raw[TFW_BOOT_EE_O_FAIL_COUNT] = 1u;
    seal(raw);
}

/* The layout, byte by byte: magic 'TFPR', layout 2, CRC little-endian. */
static int arms(const uint8_t *raw)
{
    uint16_t crc = (uint16_t)(raw[TFW_BOOT_EE_O_CRC] | (uint16_t)raw[TFW_BOOT_EE_O_CRC + 1u] << 8);

    return raw[0] == 'T' && raw[1] == 'F' && raw[2] == 'P' && raw[3] == 'R'
        && raw[TFW_BOOT_EE_O_LAYOUT] == 2u
        && tfw_boot_platform_crc(raw) == crc
        && raw[TFW_BOOT_EE_O_BOOT_STATE] == TFW_BOOT_STATE_TRIAL;
}

static void run(const uint8_t *raw)
{
    uint8_t  before[TFW_BOOT_EE_BLOCK_LEN];
    uint16_t lo, hi;
    int      want = arms(raw);

    tfw_mock_reset();
    memcpy(&tfw_mock_eeprom()[TFW_BOOT_EE_BASE], raw, TFW_BOOT_EE_BLOCK_LEN);
    memcpy(before, raw, sizeof before);

    tfw_ota_trial_start();
    tfw_ota_trial_poll();
    tfw_mock_advance(DEADLINE_MS - 1u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    tfw_mock_advance(1u);
    tfw_ota_trial_poll();

    TEST_ASSERT_EQUAL_UINT(want ? 1u : 0u, tfw_mock_watchdog_arms());
    TEST_ASSERT_EQUAL_UINT(want ? 3u : 0u, tfw_mock_watchdog_pets());
    TEST_ASSERT_EQUAL_UINT(want ? 1u : 0u, tfw_mock_reset_count());
    /* Never written back: no confirm, no fail count, no state. */
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, &tfw_mock_eeprom()[TFW_BOOT_EE_BASE], TFW_BOOT_EE_BLOCK_LEN);
    if (want) {
        armed_runs++;
    } else {
        idle_runs++;
    }
}

static void test_every_bit_flip_arms_nothing(void)
{
    uint8_t  base[TFW_BOOT_EE_BLOCK_LEN], raw[TFW_BOOT_EE_BLOCK_LEN];
    unsigned bit, n = 0u;

    clean(base, TFW_BOOT_STATE_TRIAL);
    TEST_ASSERT_TRUE(arms(base));
    for (bit = 0u; bit < TFW_BOOT_EE_BLOCK_LEN * 8u; bit++) {
        memcpy(raw, base, sizeof raw);
        raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        run(raw);
        n++;
    }
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_EE_BLOCK_LEN * 8u, n);
    TEST_ASSERT_EQUAL_UINT(0u, armed_runs);
}

static void test_every_bit_flip_under_a_resealed_crc(void)
{
    uint8_t  base[TFW_BOOT_EE_BLOCK_LEN], raw[TFW_BOOT_EE_BLOCK_LEN];
    unsigned bit, n = 0u;

    clean(base, TFW_BOOT_STATE_TRIAL);
    for (bit = 0u; bit < TFW_BOOT_EE_O_CRC * 8u; bit++) {
        memcpy(raw, base, sizeof raw);
        raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        seal(raw);
        run(raw);
        n++;
    }
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_EE_O_CRC * 8u, n);
}

static void test_erased_and_zeroed_records_arm_nothing(void)
{
    uint8_t raw[TFW_BOOT_EE_BLOCK_LEN];

    memset(raw, 0x00, sizeof raw);
    run(raw);
    memset(raw, 0xFF, sizeof raw);
    run(raw);
    /* Erased cells are not sealed: an erased record with a valid CRC still
     * carries no magic. */
    seal(raw);
    run(raw);
    TEST_ASSERT_EQUAL_UINT(0u, armed_runs);
}

static void test_random_records(void)
{
    uint8_t  raw[TFW_BOOT_EE_BLOCK_LEN];
    unsigned i, j;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        for (j = 0u; j < TFW_BOOT_EE_BLOCK_LEN; j++) {
            raw[j] = (uint8_t)xorshift32();
        }
        run(raw);
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, armed_runs + idle_runs);
}

/* Hostile fields under a valid CRC: a right header, everything else random,
 * and boot_state TRIAL half the time. */
static void test_hostile_fields_under_a_valid_crc(void)
{
    uint8_t  base[TFW_BOOT_EE_BLOCK_LEN], raw[TFW_BOOT_EE_BLOCK_LEN];
    unsigned i, j;

    clean(base, TFW_BOOT_STATE_TRIAL);
    for (i = 0u; i < HOSTILE_RUNS; i++) {
        memcpy(raw, base, sizeof raw);
        for (j = TFW_BOOT_EE_O_FLAGS; j < TFW_BOOT_EE_O_CRC; j++) {
            raw[j] = (uint8_t)xorshift32();
        }
        if ((i & 1u) == 0u) {
            raw[TFW_BOOT_EE_O_BOOT_STATE] = TFW_BOOT_STATE_TRIAL;
        }
        seal(raw);
        run(raw);
    }
    TEST_ASSERT_EQUAL_UINT(HOSTILE_RUNS, armed_runs + idle_runs);
    TEST_ASSERT_TRUE(armed_runs >= HOSTILE_RUNS / 2u);
}

/* Every layout, boot_state and ota_state byte, and every fail count with its
 * edges -- 0, 1, 0x7F, 0x80, 0xFE, 0xFF -- against each state. */
static void test_every_layout_state_and_fail_count(void)
{
    static const uint8_t fails[] = { 0x00u, 0x01u, 0x7Fu, 0x80u, 0xFEu, 0xFFu };
    uint8_t  base[TFW_BOOT_EE_BLOCK_LEN], raw[TFW_BOOT_EE_BLOCK_LEN];
    unsigned v, f, n = 0u;

    clean(base, TFW_BOOT_STATE_TRIAL);
    for (v = 0u; v < 256u; v++) {
        memcpy(raw, base, sizeof raw);
        raw[TFW_BOOT_EE_O_LAYOUT] = (uint8_t)v;
        seal(raw);
        run(raw);
        memcpy(raw, base, sizeof raw);
        raw[TFW_BOOT_EE_O_OTA_STATE] = (uint8_t)v;
        seal(raw);
        run(raw);
        for (f = 0u; f < sizeof fails; f++) {
            memcpy(raw, base, sizeof raw);
            raw[TFW_BOOT_EE_O_BOOT_STATE] = (uint8_t)v;
            raw[TFW_BOOT_EE_O_FAIL_COUNT] = fails[f];
            seal(raw);
            run(raw);
            n++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(256u * sizeof fails, n);
    /* TRIAL under each fail count, and layout 2 and each ota_state with TRIAL. */
    TEST_ASSERT_EQUAL_UINT(sizeof fails + 1u + 256u, armed_runs);
}

static void test_a_refused_read_of_a_trial_record_arms_nothing(void)
{
    uint8_t raw[TFW_BOOT_EE_BLOCK_LEN];

    clean(raw, TFW_BOOT_STATE_TRIAL);
    memcpy(&tfw_mock_eeprom()[TFW_BOOT_EE_BASE], raw, TFW_BOOT_EE_BLOCK_LEN);
    tfw_mock_eeprom_read_fails(1u);
    tfw_ota_trial_start();
    tfw_mock_advance(DEADLINE_MS);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_watchdog_arms());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_watchdog_pets());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

/* After all of it, clean records give what test_ota_trial pins. */
static void test_clean_records_afterwards(void)
{
    static const uint8_t others[] = {
        TFW_BOOT_STATE_EMPTY, TFW_BOOT_STATE_STAGED, TFW_BOOT_STATE_GOOD, TFW_BOOT_STATE_CONFIRMED,
    };
    uint8_t  raw[TFW_BOOT_EE_BLOCK_LEN];
    unsigned i;

    tfw_mock_reset();
    tfw_mock_set_millis(500u);
    clean(raw, TFW_BOOT_STATE_TRIAL);
    memcpy(&tfw_mock_eeprom()[TFW_BOOT_EE_BASE], raw, TFW_BOOT_EE_BLOCK_LEN);
    tfw_ota_trial_start();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_watchdog_arms());
    tfw_ota_trial_poll();
    tfw_mock_advance(1000u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(2u, tfw_mock_watchdog_pets());
    tfw_mock_advance(DEADLINE_MS - 1001u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    tfw_mock_advance(1u);
    tfw_ota_trial_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());

    for (i = 0u; i < sizeof others; i++) {
        clean(raw, others[i]);
        run(raw);
    }
    TEST_ASSERT_EQUAL_UINT(sizeof others, idle_runs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_bit_flip_arms_nothing);
    RUN_TEST(test_every_bit_flip_under_a_resealed_crc);
    RUN_TEST(test_erased_and_zeroed_records_arm_nothing);
    RUN_TEST(test_random_records);
    RUN_TEST(test_hostile_fields_under_a_valid_crc);
    RUN_TEST(test_every_layout_state_and_fail_count);
    RUN_TEST(test_a_refused_read_of_a_trial_record_arms_nothing);
    RUN_TEST(test_clean_records_afterwards);
    return UNITY_END();
}
