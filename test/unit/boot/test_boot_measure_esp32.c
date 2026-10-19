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
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "sha256.h"
#include "esp_sha_mock.h"

/* 1 in the classic ESP32's build, 0 in the C3's and the P4's. */
static const int classic = CONFIG_IDF_TARGET_ESP32;

#define SPAN_MAX 300u

static uint8_t data[SPAN_MAX + 4u];

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < sizeof data; i++) {
        data[i] = (uint8_t)(i * 13u + 5u);
    }
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

/* The spans the SUIT gate hashed when it was refused: a Sig_structure of 17
 * bytes of prefix, a 2-byte bstr head and the 36-byte SUIT_Digest, and a
 * wrapped manifest of a few hundred bytes. Neither is whole words. */
static void test_the_suit_spans_are_hashed(void)
{
    hash_is_sha256(data, 55u);
    hash_is_sha256(data, 257u);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
}

/* The spans that always were whole words still hash as they did: the
 * trailer's 48 signed bytes and the 64-byte image key. */
static void test_the_word_spans_still_hash(void)
{
    hash_is_sha256(data, 48u);
    hash_is_sha256(data, 64u);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_esp_sha_mock_faults());
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_suit_spans_are_hashed);
    RUN_TEST(test_the_word_spans_still_hash);
    RUN_TEST(test_every_length_at_every_alignment_is_sha256);
    RUN_TEST(test_the_hash_is_the_targets_engine);
    RUN_TEST(test_the_measurement_is_whole_words_only);
    return UNITY_END();
}
