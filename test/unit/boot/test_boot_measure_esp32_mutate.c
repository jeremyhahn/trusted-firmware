/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 layer 0's hash (boot/hal/esp32/measure_esp32.c) under seeded
 * mutation (#163), built twice as test_boot_measure_esp32.c is: over the
 * bootloader SHA, and with CONFIG_IDF_TARGET_ESP32 over the classic ROM SHA.
 *
 * The reference is a plain SHA-256 over the same bytes. Lengths sit on each
 * side of both chunk sizes (32 and 64), so a tail dropped, a chunk overread or
 * a step skipped changes the digest on either build. A reader that fails once
 * must stop the walk, give no digest and return its own status.
 *
 * Named equivalent: the base tfw_boot_measure passes (TFW_BOOT_APP_BASE is 0
 * on the host), a chunk test of < against <= (a tail of one whole chunk is
 * that chunk either way), and the word buffer's wipe and a hash of a failed
 * read (neither reaches a digest: a failed walk ends with no digest given).
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_measure.h"
#include "sha256.h"
#include "esp_sha_mock.h"
#include "sdkconfig.h"

static const int classic = CONFIG_IDF_TARGET_ESP32;

#define REGION 512u
#define BASE   128u

static uint8_t      region[REGION];
static uint32_t     fail_addr;
static tfw_status_t fail_with;
static unsigned     reads;

/* A reader over region[] that fails, once, the read starting at fail_addr. */
static tfw_status_t rd(uint32_t addr, uint8_t *buf, uint8_t len)
{
    reads++;
    if (addr == fail_addr) {
        return fail_with;
    }
    memcpy(buf, &region[addr], len);
    return TFW_OK;
}

void setUp(void)
{
    tfw_boot_mock_reset();
    tfw_esp_sha_mock_reset(classic);
    for (uint16_t i = 0u; i < REGION; i++) {
        region[i] = (uint8_t)(i * 29u + 7u);
    }
    fail_addr = 0xFFFFFFFFu;
    fail_with = TFW_ERR_CRYPTO;
    reads     = 0u;
}
void tearDown(void) {}

static void hash_matches(uint16_t len)
{
    uint8_t got[32], want[32];

    tfw_boot_hash(region, len, got);
    tfw_sha256(region, len, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(want, got, 32, "tfw_boot_hash");
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
}

static void test_hash_of_every_length_around_both_chunks_is_sha256(void)
{
    static const uint16_t lens[] = { 1u, 3u, 31u, 32u, 33u, 63u, 64u, 65u, 97u, 131u, 200u };

    for (unsigned i = 0u; i < sizeof lens / sizeof lens[0]; i++) {
        tfw_esp_sha_mock_reset(classic);
        hash_matches(lens[i]);
    }
}

static void test_hash_reads_the_last_bytes_of_its_buffer(void)
{
    uint8_t got[32], want[32];

    tfw_boot_hash(region, 66u, got);
    region[65] ^= 0x01u;
    tfw_boot_hash(region, 66u, want);
    TEST_ASSERT_FALSE(memcmp(got, want, 32) == 0);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
}

static void test_measure_walks_the_reader_from_the_base(void)
{
    uint8_t got[32], want[32];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(rd, BASE, 196u, got));
    tfw_sha256(&region[BASE], 196u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_esp_sha_mock_boot_digests());
}

static void test_measure_of_the_mock_flash_is_sha256(void)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  got[32], want[32];

    for (uint16_t i = 0u; i < 100u; i++) {
        flash[i] = (uint8_t)(i * 3u + 1u);
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(100u, got));
    tfw_sha256(flash, 100u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

static void test_measure_refuses_zero_and_a_length_not_whole_words(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(rd, BASE, 0u, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(rd, BASE, 6u, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(rd, BASE, 2u, out));
    TEST_ASSERT_EQUAL_UINT(0u, reads);
}

static void test_measure_takes_the_largest_whole_word_length_and_no_more(void)
{
    uint32_t max = TFW_BOOT_MAX_IMAGE_LEN & ~3u;
    uint8_t  out[32];

    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_MAX_IMAGE_LEN, max);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(max, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure(max + 4u, out));
}

/* The second read fails: the walk stops there, the status is the reader's,
 * and no digest is given -- out keeps what it held. */
static void test_a_failed_read_stops_the_walk_and_gives_no_digest(void)
{
    uint8_t out[32], before[32];

    memset(out, 0xEE, sizeof out);
    memcpy(before, out, sizeof out);
    fail_addr = BASE + (classic ? 64u : 32u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_measure_from(rd, BASE, 256u, out));
    TEST_ASSERT_EQUAL_UINT(2u, reads);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, out, 32);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_boot_digests());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
}

static void test_a_failed_read_returns_its_own_status(void)
{
    uint8_t out[32];

    fail_addr = BASE;
    fail_with = TFW_ERR_BAD_STATE;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_measure_from(rd, BASE, 4u, out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hash_of_every_length_around_both_chunks_is_sha256);
    RUN_TEST(test_hash_reads_the_last_bytes_of_its_buffer);
    RUN_TEST(test_measure_walks_the_reader_from_the_base);
    RUN_TEST(test_measure_of_the_mock_flash_is_sha256);
    RUN_TEST(test_measure_refuses_zero_and_a_length_not_whole_words);
    RUN_TEST(test_measure_takes_the_largest_whole_word_length_and_no_more);
    RUN_TEST(test_a_failed_read_stops_the_walk_and_gives_no_digest);
    RUN_TEST(test_a_failed_read_returns_its_own_status);
    return UNITY_END();
}
