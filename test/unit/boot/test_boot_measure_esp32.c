/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 layer 0's hash (boot/hal/esp32/measure_esp32.c, #48) over a host
 * stand-in for ESP-IDF's bootloader SHA and the classic ESP32's ROM SHA
 * (test/mock/esp_sha_mock.c), each held to its silicon's contract. The hash
 * refused every span that was not whole words with a zero digest, and the
 * SUIT manifest gate hashes the wrapped manifest and its Sig_structure, whose
 * lengths are the CBOR's: QEMU's sealed image was refused "no CDI" (status
 * 14). Every length, at every alignment, is SHA-256 of its bytes here.
 * Built twice (mk/boot.mk): as the C3 and the P4 build it, and with
 * CONFIG_IDF_TARGET_ESP32 as the classic ESP32 does.
 *
 * The measurement under seeded mutation (#163), on both builds: a reader that
 * fails once must stop the walk, give no digest and return its own status.
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

/* 1 in the classic ESP32's build, 0 in the C3's and the P4's. */
static const int classic = CONFIG_IDF_TARGET_ESP32;

#define SPAN_MAX 300u
#define REGION   512u
#define BASE     128u

static uint8_t      data[SPAN_MAX + 4u];
static uint8_t      region[REGION];
static uint32_t     fail_addr;
static tfw_status_t fail_with;
static unsigned     reads;

void setUp(void)
{
    uint32_t i;

    tfw_boot_mock_reset();
    for (i = 0u; i < sizeof data; i++) {
        data[i] = (uint8_t)(i * 13u + 5u);
    }
    for (i = 0u; i < REGION; i++) {
        region[i] = (uint8_t)(i * 29u + 7u);
    }
    fail_addr = 0xFFFFFFFFu;
    fail_with = TFW_ERR_CRYPTO;
    reads     = 0u;
    tfw_esp_sha_mock_reset(classic);
}

void tearDown(void) {}

static void hash_is_sha256(const uint8_t *p, uint16_t len)
{
    uint8_t want[32];
    uint8_t have[32];

    tfw_sha256(p, len, want);
    memset(have, 0xA5, sizeof have);
    tfw_boot_hash(p, len, have);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, have, sizeof want);
}

/* Every length up to SPAN_MAX at every byte alignment of the source, which
 * crosses the chunk and the 64-byte block at every offset. */
static void test_every_length_at_every_alignment_is_sha256(void)
{
    uint16_t len;
    uint16_t at;

    for (at = 0u; at < 4u; at++) {
        for (len = 0u; len <= SPAN_MAX; len++) {
            hash_is_sha256(&data[at], len);
        }
    }
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
}

/* Through the engine the target has: the classic ESP32's ROM, whose
 * bootloader word loop would refuse these lengths; elsewhere the bootloader
 * SHA, the ROM's own through ESP-IDF. */
static void test_the_hash_is_the_targets_engine(void)
{
    hash_is_sha256(data, 55u);
    if (classic) {
        TEST_ASSERT_EQUAL_UINT(1u, tfw_esp_sha_mock_rom_updates());
        TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_boot_digests());
    } else {
        TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_rom_updates());
        TEST_ASSERT_EQUAL_UINT(1u, tfw_esp_sha_mock_boot_digests());
    }
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
}

/* The image measurement is unchanged: whole words through the bootloader
 * SHA on every target, anything else TFW_ERR_RANGE. */
static uint8_t flash[128];

static tfw_status_t rd(uint32_t addr, uint8_t *buf, uint8_t len)
{
    memcpy(buf, &flash[addr], len);
    return TFW_OK;
}

static void test_the_measurement_is_whole_words_only(void)
{
    uint8_t want[32];
    uint8_t have[32];

    memcpy(flash, data, sizeof flash);
    tfw_sha256(flash, 100u, want);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(rd, 0u, 100u, have));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, have, sizeof want);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(rd, 0u, 101u, have));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
}

/* A reader over region[] that fails, once, the read starting at fail_addr. */
static tfw_status_t region_rd(uint32_t addr, uint8_t *buf, uint8_t len)
{
    reads++;
    if (addr == fail_addr) {
        return fail_with;
    }
    memcpy(buf, &region[addr], len);
    return TFW_OK;
}

static void test_measure_walks_the_reader_from_the_base(void)
{
    uint8_t got[32], want[32];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(region_rd, BASE, 196u, got));
    tfw_sha256(&region[BASE], 196u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_esp_sha_mock_boot_digests());
}

static void test_measure_refuses_zero_and_a_length_not_whole_words(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(region_rd, BASE, 0u, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(region_rd, BASE, 6u, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(region_rd, BASE, 2u, out));
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
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_measure_from(region_rd, BASE, 256u, out));
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
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_measure_from(region_rd, BASE, 4u, out));
}

static void test_measure_of_the_mock_flash_is_sha256(void)
{
    uint8_t *flash_at = tfw_boot_mock_flash();
    uint8_t  got[32], want[32];

    for (uint16_t i = 0u; i < 100u; i++) {
        flash_at[i] = (uint8_t)(i * 3u + 1u);
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(100u, got));
    tfw_sha256(flash_at, 100u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_length_at_every_alignment_is_sha256);
    RUN_TEST(test_the_hash_is_the_targets_engine);
    RUN_TEST(test_the_measurement_is_whole_words_only);
    RUN_TEST(test_measure_walks_the_reader_from_the_base);
    RUN_TEST(test_measure_refuses_zero_and_a_length_not_whole_words);
    RUN_TEST(test_measure_takes_the_largest_whole_word_length_and_no_more);
    RUN_TEST(test_a_failed_read_stops_the_walk_and_gives_no_digest);
    RUN_TEST(test_a_failed_read_returns_its_own_status);
    RUN_TEST(test_measure_of_the_mock_flash_is_sha256);
    return UNITY_END();
}
