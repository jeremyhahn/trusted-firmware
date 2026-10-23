/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The soft measurement under seeded mutation (#163).
 *
 * Each test here was written against a mutant of boot/measure/measure_soft.c
 * that test_boot_measure.c let through: a hash one byte short, a loop that
 * stops one byte early, a reader whose error is dropped or renamed, a read
 * that ignores the base. The reference is always a plain SHA-256 over the same
 * bytes, never the unit's own output.
 *
 * Named equivalent: the base passed by tfw_boot_measure (TFW_BOOT_APP_BASE is 0
 * on the host, so a mutant that passes 0 cannot be told apart here), and a
 * chunk test of left <= CHUNK (a 32-byte tail is a whole chunk either way).
 */

#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "boot_hal_mock.h"
#include "sha256.h"

#define REGION 512u
#define BASE   100u

static uint8_t      region[REGION];
static uint32_t     fail_at;
static tfw_status_t fail_with;

/* A reader over region[] that fails, with fail_with, on any read reaching at
 * or past fail_at. */
static tfw_status_t rd(uint32_t addr, uint8_t *buf, uint8_t len)
{
    if (addr + len > fail_at) {
        return fail_with;
    }
    memcpy(buf, &region[addr], len);
    return TFW_OK;
}

void setUp(void)
{
    tfw_boot_mock_reset();
    for (uint16_t i = 0u; i < REGION; i++) {
        region[i] = (uint8_t)(i * 13u + 5u);
    }
    fail_at   = REGION;
    fail_with = TFW_ERR_CRYPTO;
}
void tearDown(void) {}

static void test_hash_is_sha256_over_every_byte(void)
{
    uint8_t got[32], want[32];

    tfw_boot_hash(region, 33u, got);
    tfw_sha256(region, 33u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

/* 33 = one whole chunk plus one byte: a loop that stops a byte early drops it. */
static void test_a_tail_of_one_byte_is_measured(void)
{
    uint8_t got[32], want[32];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(rd, BASE, 33u, got));
    tfw_sha256(&region[BASE], 33u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

static void test_every_chunk_is_read_from_its_own_offset_past_the_base(void)
{
    uint8_t got[32], want[32];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(rd, BASE, 200u, got));
    tfw_sha256(&region[BASE], 200u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

static void test_one_byte_measures_that_byte(void)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  got[32], want[32];

    flash[0] = 0xA5u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(1u, got));
    tfw_sha256(flash, 1u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

/* A reader that fails part-way: its own status comes back, not a stand-in. */
static void test_a_reader_failure_part_way_is_returned_as_is(void)
{
    uint8_t got[32];

    fail_at = BASE + 40u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_measure_from(rd, BASE, 100u, got));
    fail_with = TFW_ERR_BAD_STATE;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_measure_from(rd, BASE, 100u, got));
}

static void test_a_reader_failure_on_the_first_chunk_is_returned(void)
{
    uint8_t got[32];

    fail_at = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_measure_from(rd, BASE, 1u, got));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hash_is_sha256_over_every_byte);
    RUN_TEST(test_a_tail_of_one_byte_is_measured);
    RUN_TEST(test_every_chunk_is_read_from_its_own_offset_past_the_base);
    RUN_TEST(test_one_byte_measures_that_byte);
    RUN_TEST(test_a_reader_failure_part_way_is_returned_as_is);
    RUN_TEST(test_a_reader_failure_on_the_first_chunk_is_returned);
    return UNITY_END();
}
