/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/esp32/hal_eeprom.c on the host, compiled against the esp32p4-poe
 * profile, over the suite's flash partitions (test/mock/esp32/esp_partition.h).
 *
 * The partitions are host buffers of the sizes boards/esp32p4-poe/partitions.csv
 * gives them, cut into ESP-IDF's 4 KB sectors (SPI_FLASH_SEC_SIZE, 0x1000), plus
 * one three-sector partition so a write can cross a sector boundary. The flash
 * is NOR: an erase sets a sector to 0xff, a write can only clear bits, and both
 * refuse an offset or size ESP-IDF refuses. Any call can be made to fail.
 *
 * The status codes are include/tfw/status.h's literals, not its names: a code
 * that renumbers fails here.
 */
#include <string.h>

#include "unity.h"
#include "esp_partition.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "hal_esp32.h"

#define ST_OK          0  /* TFW_OK */
#define ST_UNSUPPORTED 7  /* TFW_ERR_UNSUPPORTED */
#define ST_RANGE       11 /* TFW_ERR_RANGE */
#define ST_INTERNAL    18 /* TFW_ERR_INTERNAL */

#define SEC      0x1000u /* ESP-IDF's SPI_FLASH_SEC_SIZE */
#define WIDE     "tfw_wide"
#define N_PARTS  3u
#define MAX_SECS 3u

static esp_partition_t parts[N_PARTS] = {
    { NULL, ESP_PARTITION_TYPE_DATA, 0x42, 0x13000u, 1u * SEC, SEC, "tfw_eeprom", true,  false },
    { NULL, ESP_PARTITION_TYPE_DATA, 0x40, 0x11000u, 1u * SEC, SEC, "tfw_plat",   true,  false },
    { NULL, ESP_PARTITION_TYPE_DATA, 0x44, 0x20000u, 3u * SEC, SEC, WIDE,         false, false },
};
static uint8_t  store[N_PARTS][MAX_SECS * SEC];
static bool     present[N_PARTS];

static unsigned n_reads, n_erases, n_writes;
static size_t   erased_at[8];
static unsigned refuse_read, refuse_erase, refuse_write; /* 1-based call that fails; 0: none */

static uint8_t *backing(const esp_partition_t *p)
{
    for (unsigned i = 0u; i < N_PARTS; i++) {
        if (p == &parts[i]) {
            return store[i];
        }
    }
    TEST_FAIL_MESSAGE("a partition the lookup never returned");
    return NULL;
}

static esp_err_t span(const esp_partition_t *p, size_t off, size_t size)
{
    if (off > p->size) {
        return ESP_ERR_INVALID_ARG;
    }
    if (size > p->size - off) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label)
{
    TEST_ASSERT_EQUAL(ESP_PARTITION_TYPE_DATA, type);
    TEST_ASSERT_EQUAL(ESP_PARTITION_SUBTYPE_ANY, subtype);
    for (unsigned i = 0u; i < N_PARTS; i++) {
        if (present[i] && strcmp(parts[i].label, label) == 0) {
            return &parts[i];
        }
    }
    return NULL;
}

esp_err_t esp_partition_get_sha256(const esp_partition_t *partition, uint8_t *sha_256)
{
    (void)partition;
    (void)sha_256;
    TEST_FAIL_MESSAGE("the EEPROM HAL hashed a partition");
    return ESP_FAIL;
}

esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t size)
{
    esp_err_t e = span(p, off, size);

    if (++n_reads == refuse_read) {
        return ESP_FAIL;
    }
    if (e == ESP_OK) {
        memcpy(dst, backing(p) + off, size);
    }
    return e;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t size)
{
    esp_err_t e = span(p, off, size);

    if (++n_erases == refuse_erase) {
        return ESP_FAIL;
    }
    if (off % p->erase_size != 0u || size % p->erase_size != 0u) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (e == ESP_OK) {
        TEST_ASSERT_TRUE(n_erases <= sizeof erased_at / sizeof erased_at[0]);
        erased_at[n_erases - 1u] = off;
        memset(backing(p) + off, 0xff, size);
    }
    return e;
}

esp_err_t esp_partition_write(const esp_partition_t *p, size_t off, const void *src, size_t size)
{
    esp_err_t      e = span(p, off, size);
    const uint8_t *s = src;

    if (++n_writes == refuse_write) {
        return ESP_FAIL;
    }
    if (e == ESP_OK) {
        uint8_t *d = backing(p) + off;
        for (size_t i = 0u; i < size; i++) {
            d[i] &= s[i]; /* NOR: a write only clears bits */
        }
    }
    return e;
}

/* A pattern no erased or zeroed flash holds by accident. */
static void fill(uint8_t *b, size_t n, uint8_t seed)
{
    for (size_t i = 0u; i < n; i++) {
        b[i] = (uint8_t)(seed + i * 7u);
    }
}

static void reset_counts(void)
{
    n_reads = n_erases = n_writes = 0u;
    refuse_read = refuse_erase = refuse_write = 0u;
    memset(erased_at, 0, sizeof erased_at);
}

void setUp(void)
{
    for (unsigned i = 0u; i < N_PARTS; i++) {
        present[i] = true;
        fill(store[i], sizeof store[i], (uint8_t)(0x30u + i));
    }
    reset_counts();
}

void tearDown(void) {}

/* ------------------------------------------------------------ range checks */

static void test_the_eeprom_is_the_profiles_size(void)
{
    TEST_ASSERT_EQUAL_UINT32(4096u, TFW_EEPROM_SIZE);
}

static void test_a_read_or_write_past_the_eeprom_is_range_and_touches_nothing(void)
{
    uint8_t buf[2] = { 0xaa, 0x55 };
    uint8_t before[SEC];

    memcpy(before, store[0], SEC);
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_hal_eeprom_read(4095u, buf, 2u));
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_hal_eeprom_read(4096u, buf, 1u));
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_hal_eeprom_read(0xffffu, buf, 1u));
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_hal_eeprom_write(4095u, buf, 2u));
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_hal_eeprom_write(0xffffu, buf, 255u));
    TEST_ASSERT_EQUAL_UINT(0u, n_reads + n_erases + n_writes);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, store[0], SEC);
    TEST_ASSERT_EQUAL_HEX8(0xaa, buf[0]);
}

static void test_the_last_byte_of_the_eeprom_is_in_range(void)
{
    uint8_t b = 0x00;

    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_write(4095u, &b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x00, store[0][4095]);
    b = 0x77;
    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_read(4095u, &b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x00, b);
}

static void test_a_partition_span_past_its_end_is_range(void)
{
    uint8_t buf[4] = { 0 };

    TEST_ASSERT_EQUAL(ST_RANGE, tfw_esp_part_read(WIDE, 3u * SEC - 3u, buf, 4u));
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_esp_part_read(WIDE, 3u * SEC + 1u, buf, 0u));
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_esp_part_write(WIDE, 3u * SEC - 3u, buf, 4u));
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_esp_part_write(WIDE, 1u, buf, 0xffffffffu));
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_esp_part_write(WIDE, 0xffffffffu, buf, 2u));
    TEST_ASSERT_EQUAL_UINT(0u, n_reads + n_erases + n_writes);
}

static void test_a_zero_length_is_ok_and_moves_nothing(void)
{
    uint8_t buf[1] = { 0xaa };
    uint8_t before[SEC];

    memcpy(before, store[0], SEC);
    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_read(0u, buf, 0u));
    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_read(4096u, buf, 0u));
    TEST_ASSERT_EQUAL_HEX8(0xaa, buf[0]);
    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_write(0u, buf, 0u));
    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_write(4096u, buf, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, n_erases + n_writes);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, store[0], SEC);
}

/* ------------------------------------------------------------- the writes */

static void test_a_write_reads_back_and_rewrites_only_its_sector(void)
{
    uint8_t in[200], out[200], before[SEC];

    fill(in, sizeof in, 0x91u);
    memcpy(before, store[0], SEC);
    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_write(1000u, in, 200u));
    TEST_ASSERT_EQUAL_UINT(1u, n_erases);
    TEST_ASSERT_EQUAL_UINT(1u, n_writes);
    TEST_ASSERT_EQUAL_UINT(0u, erased_at[0]);

    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_read(1000u, out, 200u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, sizeof in);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, store[0], 1000u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before + 1200, store[0] + 1200, SEC - 1200u);
}

static void test_a_write_across_a_sector_boundary_erases_exactly_the_two_it_touches(void)
{
    uint8_t in[300], out[300], before[3u * SEC];
    const uint32_t at = 2u * SEC - 100u;

    fill(in, sizeof in, 0x5au);
    memcpy(before, store[2], sizeof before);
    TEST_ASSERT_EQUAL(ST_OK, tfw_esp_part_write(WIDE, at, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT(2u, n_erases);
    TEST_ASSERT_EQUAL_UINT(2u, n_writes);
    TEST_ASSERT_EQUAL_UINT(1u * SEC, erased_at[0]);
    TEST_ASSERT_EQUAL_UINT(2u * SEC, erased_at[1]);

    TEST_ASSERT_EQUAL(ST_OK, tfw_esp_part_read(WIDE, at, out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, sizeof in);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, store[2], at);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before + at + sizeof in, store[2] + at + sizeof in,
                                 sizeof before - at - sizeof in);
}

static void test_a_write_ending_on_a_boundary_touches_one_sector(void)
{
    uint8_t in[100];

    fill(in, sizeof in, 0x13u);
    TEST_ASSERT_EQUAL(ST_OK, tfw_esp_part_write(WIDE, SEC - 100u, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT(1u, n_erases);
    TEST_ASSERT_EQUAL_UINT(0u, erased_at[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, store[2] + SEC - 100u, sizeof in);
}

static void test_a_write_of_the_bytes_already_there_neither_erases_nor_writes(void)
{
    uint8_t same[300];

    memcpy(same, store[0] + 7u, 255u);
    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_eeprom_write(7u, same, 255u));
    TEST_ASSERT_EQUAL_UINT(0u, n_erases);
    TEST_ASSERT_EQUAL_UINT(0u, n_writes);

    reset_counts();
    memcpy(same, store[2] + 2u * SEC - 150u, sizeof same);
    TEST_ASSERT_EQUAL(ST_OK, tfw_esp_part_write(WIDE, 2u * SEC - 150u, same, sizeof same));
    TEST_ASSERT_EQUAL_UINT(0u, n_erases);
    TEST_ASSERT_EQUAL_UINT(0u, n_writes);
}

static void test_a_write_rewrites_only_the_sector_whose_bytes_change(void)
{
    uint8_t in[300];
    const uint32_t at = 2u * SEC - 150u;

    memcpy(in, store[2] + at, sizeof in);
    in[200] ^= 0xffu; /* in the second sector only */
    TEST_ASSERT_EQUAL(ST_OK, tfw_esp_part_write(WIDE, at, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT(1u, n_erases);
    TEST_ASSERT_EQUAL_UINT(1u, n_writes);
    TEST_ASSERT_EQUAL_UINT(2u * SEC, erased_at[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, store[2] + at, sizeof in);
}

/* --------------------------------------------------------- no partition */

static void test_no_partition_is_unsupported(void)
{
    uint8_t buf[4] = { 0 };

    present[0] = false;
    present[1] = false;
    TEST_ASSERT_EQUAL(ST_UNSUPPORTED, tfw_hal_eeprom_read(0u, buf, 4u));
    TEST_ASSERT_EQUAL(ST_UNSUPPORTED, tfw_hal_eeprom_write(0u, buf, 4u));
    TEST_ASSERT_EQUAL(ST_UNSUPPORTED, tfw_hal_nvm_read(0u, buf, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, n_reads + n_erases + n_writes);
}

/* ------------------------------------------------------------- refusals */

static void test_a_refused_read_is_internal(void)
{
    uint8_t buf[4] = { 0 };

    refuse_read = 1u;
    TEST_ASSERT_EQUAL(ST_INTERNAL, tfw_hal_eeprom_read(0u, buf, 4u));
    reset_counts();
    refuse_read = 1u;
    TEST_ASSERT_EQUAL(ST_INTERNAL, tfw_hal_nvm_read(0u, buf, 4u));
}

static void test_a_write_whose_sector_read_is_refused_is_internal_and_changes_nothing(void)
{
    uint8_t in[16], before[SEC];

    fill(in, sizeof in, 0xc3u);
    memcpy(before, store[0], SEC);
    refuse_read = 1u;
    TEST_ASSERT_EQUAL(ST_INTERNAL, tfw_hal_eeprom_write(64u, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT(0u, n_erases + n_writes);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, store[0], SEC);
}

static void test_a_refused_erase_is_internal_writes_nothing_and_changes_nothing(void)
{
    uint8_t in[16], before[SEC];

    fill(in, sizeof in, 0xc3u);
    memcpy(before, store[0], SEC);
    refuse_erase = 1u;
    TEST_ASSERT_EQUAL(ST_INTERNAL, tfw_hal_eeprom_write(64u, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT(0u, n_writes);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, store[0], SEC);
}

static void test_a_refused_write_is_internal(void)
{
    uint8_t in[16];

    fill(in, sizeof in, 0xc3u);
    refuse_write = 1u;
    TEST_ASSERT_EQUAL(ST_INTERNAL, tfw_hal_eeprom_write(64u, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT(1u, n_erases);
}

/* A refusal in the second sector leaves the first one written and no sector
 * past the refusal touched: the write is sector by sector, not atomic. */
static void test_a_refusal_in_the_second_sector_stops_there(void)
{
    uint8_t in[300], before[3u * SEC];
    const uint32_t at = 2u * SEC - 100u;

    fill(in, sizeof in, 0x5au);
    memcpy(before, store[2], sizeof before);
    refuse_read = 2u;
    TEST_ASSERT_EQUAL(ST_INTERNAL, tfw_esp_part_write(WIDE, at, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT(1u, n_erases);
    TEST_ASSERT_EQUAL_UINT(1u, n_writes);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, store[2] + at, 100u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before + 2u * SEC, store[2] + 2u * SEC, SEC);
}

/* ------------------------------------------------------ the platform block */

static void test_the_platform_block_reads_its_own_partition(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_nvm_read(16u, out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(store[1] + 16, out, sizeof out);
    TEST_ASSERT_EQUAL(ST_RANGE, tfw_hal_nvm_read(4095u, out, 2u));
    present[0] = false; /* not the EEPROM's */
    TEST_ASSERT_EQUAL(ST_OK, tfw_hal_nvm_read(0u, out, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_eeprom_is_the_profiles_size);
    RUN_TEST(test_a_read_or_write_past_the_eeprom_is_range_and_touches_nothing);
    RUN_TEST(test_the_last_byte_of_the_eeprom_is_in_range);
    RUN_TEST(test_a_partition_span_past_its_end_is_range);
    RUN_TEST(test_a_zero_length_is_ok_and_moves_nothing);
    RUN_TEST(test_a_write_reads_back_and_rewrites_only_its_sector);
    RUN_TEST(test_a_write_across_a_sector_boundary_erases_exactly_the_two_it_touches);
    RUN_TEST(test_a_write_ending_on_a_boundary_touches_one_sector);
    RUN_TEST(test_a_write_of_the_bytes_already_there_neither_erases_nor_writes);
    RUN_TEST(test_a_write_rewrites_only_the_sector_whose_bytes_change);
    RUN_TEST(test_no_partition_is_unsupported);
    RUN_TEST(test_a_refused_read_is_internal);
    RUN_TEST(test_a_write_whose_sector_read_is_refused_is_internal_and_changes_nothing);
    RUN_TEST(test_a_refused_erase_is_internal_writes_nothing_and_changes_nothing);
    RUN_TEST(test_a_refused_write_is_internal);
    RUN_TEST(test_a_refusal_in_the_second_sector_stops_there);
    RUN_TEST(test_the_platform_block_reads_its_own_partition);
    return UNITY_END();
}
