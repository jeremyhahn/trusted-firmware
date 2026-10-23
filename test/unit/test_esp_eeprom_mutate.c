/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 EEPROM capability and platform block,
 * src/hal/esp32/hal_eeprom.c.
 *
 * The unit had no host build. This builds it over a stand-in for the two
 * data partitions it names, as NOR flash: an erase sets a 4 KB sector to
 * 0xFF and a write can only clear bits, so a write with no erase before it
 * leaves the wrong bytes. It holds:
 *
 *   - each range check at its edge, for a partition read and write, and for
 *     the EEPROM capability at TFW_EEPROM_SIZE;
 *   - seeded random writes, across sector boundaries, against a shadow copy,
 *     with one erase per sector whose bytes change and none for one whose
 *     bytes do not;
 *   - each ESP-IDF error mapped to TFW_ERR_INTERNAL, an unknown label to
 *     TFW_ERR_UNSUPPORTED;
 *   - the EEPROM on tfw_eeprom and the platform block on tfw_plat.
 *
 * Deterministic: xorshift32 from a literal seed, literal counts.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_partition.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "hal_esp32.h"

#define SECTOR      4096u
#define EE_PART_LEN (2u * SECTOR)   /* larger than TFW_EEPROM_SIZE: the cap is the unit's */
#define PLAT_LEN    SECTOR
#define RANDOM_RUNS 4000u

static const esp_partition_t parts[2] = {
    { NULL, ESP_PARTITION_TYPE_DATA, 0x99, 0x110000u, EE_PART_LEN, SECTOR, TFW_ESP_PART_EEPROM, false, false },
    { NULL, ESP_PARTITION_TYPE_DATA, 0x99, 0x112000u, PLAT_LEN, SECTOR, TFW_ESP_PART_PLATFORM, false, false },
};

static uint8_t   flash[2][EE_PART_LEN];
static uint8_t   shadow[EE_PART_LEN];
static unsigned  finds, reads, erases, writes, misuse;
static esp_err_t read_err, erase_err, write_err;
static uint32_t  rng;

static int part_of(const esp_partition_t *p)
{
    return p == &parts[0] ? 0 : p == &parts[1] ? 1 : -1;
}

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label)
{
    unsigned i;

    finds++;
    if (type != ESP_PARTITION_TYPE_DATA || subtype != ESP_PARTITION_SUBTYPE_ANY || label == NULL) {
        misuse++;
        return NULL;
    }
    for (i = 0u; i < 2u; i++) {
        if (strcmp(parts[i].label, label) == 0) {
            return &parts[i];
        }
    }
    return NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size)
{
    const int i = part_of(partition);

    reads++;
    if (i < 0 || src_offset > partition->size || size > partition->size - src_offset) {
        misuse++;
        return ESP_ERR_INVALID_ARG;
    }
    if (read_err != ESP_OK) {
        return read_err;
    }
    memcpy(dst, &flash[i][src_offset], size);
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size)
{
    const int i = part_of(partition);

    erases++;
    if (i < 0 || offset % SECTOR != 0u || size != SECTOR || offset >= partition->size) {
        misuse++;
        return ESP_ERR_INVALID_ARG;
    }
    if (erase_err != ESP_OK) {
        return erase_err;
    }
    memset(&flash[i][offset], 0xFF, size);
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *partition, size_t dst_offset, const void *src, size_t size)
{
    const int      i = part_of(partition);
    const uint8_t *s = src;
    size_t         k;

    writes++;
    if (i < 0 || dst_offset > partition->size || size > partition->size - dst_offset) {
        misuse++;
        return ESP_ERR_INVALID_ARG;
    }
    if (write_err != ESP_OK) {
        return write_err;
    }
    for (k = 0u; k < size; k++) {
        flash[i][dst_offset + k] &= s[k];   /* NOR: a write only clears bits */
    }
    return ESP_OK;
}

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    unsigned k;

    rng = 0x6A09E667u;
    for (k = 0u; k < EE_PART_LEN; k++) {
        flash[0][k] = (uint8_t)rnd();
        flash[1][k] = (uint8_t)rnd();
    }
    memcpy(shadow, flash[0], sizeof shadow);
    finds = reads = erases = writes = misuse = 0u;
    read_err = erase_err = write_err = ESP_OK;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, misuse);
}

/* ------------------------------------------------------- partition read */

static void test_a_partition_read_at_each_edge(void)
{
    uint8_t buf[8];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_read(TFW_ESP_PART_EEPROM, EE_PART_LEN, buf, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_esp_part_read(TFW_ESP_PART_EEPROM, EE_PART_LEN + 1u, buf, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_read(TFW_ESP_PART_EEPROM, EE_PART_LEN - 8u, buf, 8u));
    TEST_ASSERT_EQUAL_MEMORY(&flash[0][EE_PART_LEN - 8u], buf, 8u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_esp_part_read(TFW_ESP_PART_EEPROM, EE_PART_LEN - 7u, buf, 8u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_esp_part_read(TFW_ESP_PART_EEPROM, 8u, buf, 0xFFFFFFFCu));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_read(TFW_ESP_PART_PLATFORM, PLAT_LEN - 8u, buf, 8u));
    TEST_ASSERT_EQUAL_MEMORY(&flash[1][PLAT_LEN - 8u], buf, 8u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_esp_part_read(TFW_ESP_PART_PLATFORM, PLAT_LEN - 7u, buf, 8u));
}

static void test_a_partition_read_maps_its_failures(void)
{
    uint8_t buf[8];

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_esp_part_read("tfw_none", 0u, buf, 8u));
    read_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_part_read(TFW_ESP_PART_EEPROM, 0u, buf, 8u));
}

/* ------------------------------------------------------ partition write */

static void test_a_partition_write_at_each_edge(void)
{
    const uint8_t buf[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_write(TFW_ESP_PART_EEPROM, EE_PART_LEN, buf, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, erases);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_esp_part_write(TFW_ESP_PART_EEPROM, EE_PART_LEN + 1u, buf, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_esp_part_write(TFW_ESP_PART_EEPROM, EE_PART_LEN - 7u, buf, 8u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_esp_part_write(TFW_ESP_PART_EEPROM, 8u, buf, 0xFFFFFFFCu));
    TEST_ASSERT_EQUAL_UINT(0u, reads);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_write(TFW_ESP_PART_EEPROM, EE_PART_LEN - 8u, buf, 8u));
    TEST_ASSERT_EQUAL_MEMORY(buf, &flash[0][EE_PART_LEN - 8u], 8u);
    TEST_ASSERT_EQUAL_MEMORY(shadow, flash[0], EE_PART_LEN - 8u);
    TEST_ASSERT_EQUAL_UINT(1u, erases);
}

static void test_a_write_across_a_sector_rewrites_both(void)
{
    uint8_t  buf[16];
    unsigned k;

    for (k = 0u; k < sizeof buf; k++) {
        buf[k] = (uint8_t)~shadow[SECTOR - 8u + k];
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_write(TFW_ESP_PART_EEPROM, SECTOR - 8u, buf, sizeof buf));
    memcpy(&shadow[SECTOR - 8u], buf, sizeof buf);
    TEST_ASSERT_EQUAL_MEMORY(shadow, flash[0], EE_PART_LEN);
    TEST_ASSERT_EQUAL_UINT(2u, reads);
    TEST_ASSERT_EQUAL_UINT(2u, erases);
    TEST_ASSERT_EQUAL_UINT(2u, writes);
}

static void test_an_unchanged_write_neither_erases_nor_writes(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_write(TFW_ESP_PART_EEPROM, SECTOR - 100u, &shadow[SECTOR - 100u], 200u));
    TEST_ASSERT_EQUAL_UINT(2u, reads);
    TEST_ASSERT_EQUAL_UINT(0u, erases);
    TEST_ASSERT_EQUAL_UINT(0u, writes);
    TEST_ASSERT_EQUAL_MEMORY(shadow, flash[0], EE_PART_LEN);
}

/* One byte changed in the second sector of a two-sector write: only that
 * sector is erased. */
static void test_only_the_sector_that_changes_is_erased(void)
{
    uint8_t buf[200];

    memcpy(buf, &shadow[SECTOR - 100u], sizeof buf);
    buf[150] ^= 0x5Au;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_write(TFW_ESP_PART_EEPROM, SECTOR - 100u, buf, sizeof buf));
    shadow[SECTOR + 50u] ^= 0x5Au;
    TEST_ASSERT_EQUAL_MEMORY(shadow, flash[0], EE_PART_LEN);
    TEST_ASSERT_EQUAL_UINT(1u, erases);
    TEST_ASSERT_EQUAL_UINT(1u, writes);
}

static void test_seeded_random_writes_match_a_shadow(void)
{
    static uint8_t buf[EE_PART_LEN];
    unsigned       run;
    unsigned       want_erases = 0u;

    for (run = 0u; run < RANDOM_RUNS; run++) {
        const uint32_t off = rnd() % EE_PART_LEN;
        const uint32_t len = (rnd() % 4u == 0u) ? rnd() % (EE_PART_LEN - off + 1u)
                                                : rnd() % (EE_PART_LEN - off < 64u ? EE_PART_LEN - off + 1u : 65u);
        uint32_t       k;
        uint32_t       s;

        for (k = 0u; k < len; k++) {
            buf[k] = (rnd() % 3u == 0u) ? shadow[off + k] : (uint8_t)rnd();
        }
        for (s = off / SECTOR; len > 0u && s <= (off + len - 1u) / SECTOR; s++) {
            const uint32_t lo = off > s * SECTOR ? off : s * SECTOR;
            const uint32_t hi = off + len < (s + 1u) * SECTOR ? off + len : (s + 1u) * SECTOR;
            if (memcmp(&shadow[lo], &buf[lo - off], hi - lo) != 0) {
                want_erases++;
            }
        }
        TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_part_write(TFW_ESP_PART_EEPROM, off, buf, len));
        memcpy(&shadow[off], buf, len);
        if (memcmp(shadow, flash[0], EE_PART_LEN) != 0) {
            printf("run %u: off %u len %u\n", run, (unsigned)off, (unsigned)len);
            TEST_FAIL_MESSAGE("the partition is not the shadow");
        }
        TEST_ASSERT_EQUAL_UINT(want_erases, erases);
    }
    TEST_ASSERT_TRUE(want_erases > RANDOM_RUNS / 2u);
}

static void test_a_partition_write_maps_its_failures(void)
{
    const uint8_t buf[4] = { 0, 0, 0, 0 };

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_esp_part_write("tfw_none", 0u, buf, 4u));
    read_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_part_write(TFW_ESP_PART_EEPROM, 0u, buf, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, erases);
    read_err  = ESP_OK;
    erase_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_part_write(TFW_ESP_PART_EEPROM, 0u, buf, 4u));
    TEST_ASSERT_EQUAL_UINT(0u, writes);
    erase_err = ESP_OK;
    write_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_part_write(TFW_ESP_PART_EEPROM, 0u, buf, 4u));
    /* the first sector failed: nothing is written past it */
    write_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_part_write(TFW_ESP_PART_EEPROM, SECTOR - 2u, buf, 4u));
    TEST_ASSERT_EQUAL_UINT(2u, writes);
}

/* -------------------------------------------- the EEPROM and the block */

static void test_the_eeprom_ends_at_its_size(void)
{
    uint8_t buf[2] = { 0x11, 0x22 };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write((uint16_t)(TFW_EEPROM_SIZE - 2u), buf, 2u));
    TEST_ASSERT_EQUAL_MEMORY(buf, &flash[0][TFW_EEPROM_SIZE - 2u], 2u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)(TFW_EEPROM_SIZE - 1u), buf, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)TFW_EEPROM_SIZE, buf, 0u + 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(0xFFFFu, buf, 2u));
    TEST_ASSERT_EQUAL_UINT(1u, finds);

    memset(buf, 0, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read((uint16_t)(TFW_EEPROM_SIZE - 2u), buf, 2u));
    TEST_ASSERT_EQUAL_HEX8(0x11, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22, buf[1]);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_read((uint16_t)(TFW_EEPROM_SIZE - 1u), buf, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_read(0xFFFFu, buf, 2u));
    TEST_ASSERT_EQUAL_UINT(2u, finds);
}

static void test_the_eeprom_and_the_platform_block_are_their_own_partitions(void)
{
    uint8_t buf[16];
    uint8_t plat[PLAT_LEN];

    memcpy(plat, flash[1], sizeof plat);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read(16u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_MEMORY(&flash[0][16], buf, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_nvm_read(16u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_MEMORY(&flash[1][16], buf, sizeof buf);
    /* the platform block is read where the EEPROM's size would refuse */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_nvm_read((uint16_t)(PLAT_LEN - 16u), buf, sizeof buf));
    TEST_ASSERT_EQUAL_MEMORY(&flash[1][PLAT_LEN - 16u], buf, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_nvm_read((uint16_t)(PLAT_LEN - 15u), buf, sizeof buf));
    memset(buf, 0, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(16u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_MEMORY(buf, &flash[0][16], sizeof buf);
    TEST_ASSERT_EQUAL_MEMORY(plat, flash[1], sizeof plat);
    TEST_ASSERT_EQUAL_UINT(1u, erases);
    TEST_ASSERT_EQUAL_UINT(1u, writes);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_partition_read_at_each_edge);
    RUN_TEST(test_a_partition_read_maps_its_failures);
    RUN_TEST(test_a_partition_write_at_each_edge);
    RUN_TEST(test_a_write_across_a_sector_rewrites_both);
    RUN_TEST(test_an_unchanged_write_neither_erases_nor_writes);
    RUN_TEST(test_only_the_sector_that_changes_is_erased);
    RUN_TEST(test_seeded_random_writes_match_a_shadow);
    RUN_TEST(test_a_partition_write_maps_its_failures);
    RUN_TEST(test_the_eeprom_ends_at_its_size);
    RUN_TEST(test_the_eeprom_and_the_platform_block_are_their_own_partitions);
    return UNITY_END();
}
