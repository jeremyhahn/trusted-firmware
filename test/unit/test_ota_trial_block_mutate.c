/*
 * Mutation tests for src/core/ota_trial_block.c, tfw_ota_trial_on on its own:
 * the one read of the platform block, the parse and the TRIAL compare. Each
 * test names the faults it kills -- a dropped read or parse status, the
 * compare flipped or pointed at another state, the read moved or shortened
 * by one, an && turned to ||, a constant return.
 *
 * Two faults leave the locals unwritten (a refused read, a refused parse).
 * Called twice in a row from the same frame, the second call sees the first
 * call's stack, so the first call leaves a good TRIAL record there: a fault
 * that ignores either status then answers 1 where the unit answers 0.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "boot_platform.h"
#include "ota_trial.h"

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) { }

static void seal(uint8_t *raw)
{
    uint16_t crc = tfw_boot_platform_crc(raw);

    raw[TFW_BOOT_EE_O_CRC]      = (uint8_t)crc;
    raw[TFW_BOOT_EE_O_CRC + 1u] = (uint8_t)(crc >> 8);
}

static void record(uint8_t *raw, uint8_t boot_state)
{
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    memset(raw, 0, TFW_BOOT_EE_LEN);
    p.boot_state = boot_state;
    tfw_boot_platform_serialise(&p, raw);
    raw[TFW_BOOT_EE_O_FAIL_COUNT] = 1u;
    seal(raw);
}

static void load(const uint8_t *raw)
{
    memcpy(&tfw_mock_eeprom()[TFW_BOOT_EE_BASE], raw, TFW_BOOT_EE_LEN);
}

/* Kills: compare flipped to !=, a constant 0, the read moved by one. */
static void test_a_trial_record_is_on(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    uint16_t lo, hi;

    record(raw, TFW_BOOT_STATE_TRIAL);
    load(raw);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
}

/* Kills: the compare pointed at any other state, a constant 1. Every value of
 * the byte, so a compare widened to <= or >= is caught on both sides. */
static void test_every_other_state_is_off(void)
{
    uint8_t  raw[TFW_BOOT_EE_LEN];
    unsigned v, on = 0u;

    for (v = 0u; v < 256u; v++) {
        record(raw, (uint8_t)v);
        load(raw);
        if (tfw_ota_trial_on() != 0u) {
            TEST_ASSERT_EQUAL_UINT(TFW_BOOT_STATE_TRIAL, v);
            on++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(1u, on);
}

/* Kills: the read's status ignored, and the first && turned to || (a good
 * read then skips the parse and the state). */
static void test_a_refused_read_is_off(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    uint8_t first, second;

    record(raw, TFW_BOOT_STATE_TRIAL);
    load(raw);
    /* No assert between the two calls: Unity's frame would overwrite the
     * stack the first call leaves behind. */
    first = tfw_ota_trial_on();
    tfw_mock_eeprom_read_fails(1u);
    second = tfw_ota_trial_on();
    TEST_ASSERT_EQUAL_UINT8(1u, first);
    TEST_ASSERT_EQUAL_UINT8(0u, second);
    tfw_mock_eeprom_read_fails(0u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
}

/* Kills: the parse's status ignored, and the second && turned to ||. Each
 * refusal the parse makes -- magic, layout, CRC -- over a TRIAL state byte. */
static void test_a_refused_parse_is_off(void)
{
    uint8_t good[TFW_BOOT_EE_LEN], raw[TFW_BOOT_EE_LEN];

    record(good, TFW_BOOT_STATE_TRIAL);

    memcpy(raw, good, sizeof raw);
    raw[TFW_BOOT_EE_O_CRC] ^= 0x01u;
    load(good);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
    load(raw);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_ota_trial_on());

    memcpy(raw, good, sizeof raw);
    raw[TFW_BOOT_EE_O_LAYOUT] ^= 0x01u;
    seal(raw);
    load(good);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
    load(raw);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_ota_trial_on());

    memcpy(raw, good, sizeof raw);
    raw[TFW_BOOT_EE_O_MAGIC] ^= 0x01u;
    seal(raw);
    load(good);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
    load(raw);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_ota_trial_on());
}

/* Kills: the read shortened by one. A zeroed block read first leaves zero in
 * the stack byte under the CRC's high half, which a one-short read would keep. */
static void test_the_whole_block_is_read(void)
{
    uint8_t zero[TFW_BOOT_EE_LEN], raw[TFW_BOOT_EE_LEN];

    record(raw, TFW_BOOT_STATE_TRIAL);
    TEST_ASSERT_NOT_EQUAL(0u, raw[TFW_BOOT_EE_LEN - 1u]);
    memset(zero, 0, sizeof zero);
    load(zero);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_ota_trial_on());
    load(raw);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_trial_record_is_on);
    RUN_TEST(test_every_other_state_is_off);
    RUN_TEST(test_a_refused_read_is_off);
    RUN_TEST(test_a_refused_parse_is_off);
    RUN_TEST(test_the_whole_block_is_read);
    return UNITY_END();
}
