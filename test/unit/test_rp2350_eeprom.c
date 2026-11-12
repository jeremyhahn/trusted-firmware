/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350's flash EEPROM (src/hal/cortex-m/rp2350/hal_eeprom.c) over a NOR
 * flash model. The board's last two sectors, the user sector and the platform
 * block's, are mapped at their XIP addresses, so the HAL's reads through XIP
 * are the ones the image makes. tfw-boot's read-modify-write (rp_flash.c) is
 * linked as the image links it; the model stands in under it, in the place of
 * rp_flash_rom.c's one-sector read and write: the write erases the sector to
 * 0xFF and then programs it, and programming only clears bits (new = old &
 * data), as NOR does. Every erase is recorded, and a hook flips a byte after
 * the program so the read-back sees a mismatch.
 */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "rp_flash.h"

#define WINDOW_BASE  ((uint32_t)TFW_CM_USER_EE_BASE)
#define WINDOW_LEN   ((uint32_t)TFW_FLASH_TOTAL - WINDOW_BASE)
#define FLASH        ((uint8_t *)(uintptr_t)TFW_RP_XIP_BASE)
#define USER_OFF     ((uint32_t)TFW_CM_USER_EE_BASE - TFW_RP_XIP_BASE)
#define PLATFORM_OFF ((uint32_t)TFW_CM_PLATFORM_BASE - TFW_RP_XIP_BASE)
#define USER_EE      (&FLASH[USER_OFF])
#define PLATFORM     (&FLASH[PLATFORM_OFF])
#define MAX_ERASES   8u

static uint32_t erases[MAX_ERASES];
static unsigned n_erases;
static int      corrupt_next;   /* flip a byte after the next program */

void tfw_rp_flash_sector_read(uint32_t off, uint8_t *buf)
{
    memcpy(buf, &FLASH[off], TFW_RP_FLASH_SECTOR);
}

tfw_status_t tfw_rp_flash_sector_write(uint32_t off, const uint8_t *buf)
{
    uint32_t i;

    TEST_ASSERT_EQUAL_UINT32(0u, off % TFW_RP_FLASH_SECTOR);
    TEST_ASSERT_TRUE(n_erases < MAX_ERASES);
    erases[n_erases++] = off;
    memset(&FLASH[off], 0xFF, TFW_RP_FLASH_SECTOR);
    for (i = 0u; i < TFW_RP_FLASH_SECTOR; i++) {
        FLASH[off + i] &= buf[i];
    }
    if (corrupt_next) {
        corrupt_next = 0;
        FLASH[off + TFW_RP_FLASH_SECTOR - 1u] ^= 0x01u;
    }
    return TFW_OK;
}

void setUp(void)
{
    memset(USER_EE, 0xFF, TFW_RP_FLASH_SECTOR);
    memset(PLATFORM, 0xA5, TFW_RP_FLASH_SECTOR);
    n_erases     = 0u;
    corrupt_next = 0;
}

void tearDown(void) {}

static void assert_platform_untouched(void)
{
    unsigned i;

    for (i = 0u; i < n_erases; i++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(PLATFORM_OFF, erases[i]);
    }
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, PLATFORM, TFW_RP_FLASH_SECTOR);
}

/* tfw_rp_flash_sector_write's model: an erase is all ones, a program only
 * clears bits, so programming over unerased bytes cannot set one. */
static void test_the_model_erases_to_ones_and_programs_only_clear_bits(void)
{
    uint8_t buf[TFW_RP_FLASH_SECTOR];

    memset(USER_EE, 0x0F, TFW_RP_FLASH_SECTOR);
    memset(buf, 0xF0, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_rp_flash_sector_write(USER_OFF, buf));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xF0, USER_EE, TFW_RP_FLASH_SECTOR);
    TEST_ASSERT_EQUAL_UINT(1u, n_erases);
}

/* The EEPROM address past tfw-boot's span is the same byte of the user
 * sector, through write and read. */
static void test_a_user_address_lands_at_its_byte_of_the_user_sector(void)
{
    static const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t back[8];
    uint16_t addr = (uint16_t)(TFW_BOOT_EE_LEN + 0x40u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(addr, data, sizeof data));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, &USER_EE[0x40], sizeof data);
    TEST_ASSERT_EQUAL_HEX8(0xFF, USER_EE[0x3F]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, USER_EE[0x48]);
    TEST_ASSERT_EQUAL_UINT(1u, n_erases);
    TEST_ASSERT_EQUAL_HEX32(USER_OFF, erases[0]);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read(addr, back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, back, sizeof back);
    assert_platform_untouched();
}

/* A second write keeps the first's bytes: the sector is read, merged and
 * written whole, never erased with the old bytes lost. */
static void test_a_second_write_keeps_the_rest_of_the_sector(void)
{
    static const uint8_t a[4] = {0x11, 0x22, 0x33, 0x44};
    static const uint8_t b[4] = {0xEE, 0xDD, 0xCC, 0xBB};

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write((uint16_t)TFW_BOOT_EE_LEN, a, sizeof a));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write((uint16_t)(TFW_BOOT_EE_LEN + 2u), b, sizeof b));
    TEST_ASSERT_EQUAL_HEX8(0x11, USER_EE[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22, USER_EE[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, &USER_EE[2], sizeof b);
    TEST_ASSERT_EQUAL_UINT(2u, n_erases);
    assert_platform_untouched();
}

/* The last byte below TFW_EEPROM_SIZE is the user span's last; one past it
 * is TFW_ERR_RANGE with nothing erased. */
static void test_the_last_byte_lands_and_one_past_the_size_is_range(void)
{
    uint8_t one = 0x5Au;
    uint8_t two[2] = {0u, 0u};
    uint16_t last = (uint16_t)(TFW_EEPROM_SIZE - 1u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(last, &one, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5A, USER_EE[last - TFW_BOOT_EE_LEN]);
    n_erases = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(last, two, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_read(last, two, 2u));
    TEST_ASSERT_EQUAL_UINT(0u, n_erases);
    assert_platform_untouched();
}

/* Below TFW_BOOT_EE_LEN only the confirm request is written, into the
 * platform window as the generic file stores it, with no flash erase; any
 * other write there is TFW_ERR_RANGE and changes nothing. */
static void test_the_boot_area_takes_only_the_confirm_request(void)
{
    uint8_t req[TFW_BOOT_CONFIRM_LEN];
    uint8_t one = 0u;

    memset(req, 0x3C, sizeof req);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(0u, &one, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      tfw_hal_eeprom_write((uint16_t)(TFW_BOOT_CONFIRM_ADDR + 1u), req,
                                           (uint8_t)(TFW_BOOT_CONFIRM_LEN - 1u)));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      tfw_hal_eeprom_write((uint16_t)(TFW_BOOT_EE_LEN - 1u), req, 2u));
    assert_platform_untouched();
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFF, USER_EE, TFW_RP_FLASH_SECTOR);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, req, sizeof req));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(req, &PLATFORM[TFW_BOOT_CONFIRM_ADDR], sizeof req);
    TEST_ASSERT_EQUAL_UINT(0u, n_erases);
}

/* A read across the boot area's end takes the boot bytes from the platform
 * window and the rest from the user sector. */
static void test_a_read_across_the_boot_areas_end_joins_both(void)
{
    uint8_t back[4];

    USER_EE[0] = 0x77u;
    USER_EE[1] = 0x88u;
    TEST_ASSERT_EQUAL(TFW_OK,
                      tfw_hal_eeprom_read((uint16_t)(TFW_BOOT_EE_LEN - 2u), back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8(PLATFORM[TFW_BOOT_EE_LEN - 2u], back[0]);
    TEST_ASSERT_EQUAL_HEX8(PLATFORM[TFW_BOOT_EE_LEN - 1u], back[1]);
    TEST_ASSERT_EQUAL_HEX8(0x77, back[2]);
    TEST_ASSERT_EQUAL_HEX8(0x88, back[3]);
}

/* A write whose user bytes all reach the end of the user sector erases
 * that sector only, never the platform block's above it. */
static void test_a_write_to_the_user_sectors_end_never_erases_the_platform_sector(void)
{
    uint8_t data[16];
    uint16_t addr = (uint16_t)(TFW_EEPROM_SIZE - sizeof data);

    memset(data, 0x00, sizeof data);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(addr, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT(1u, n_erases);
    TEST_ASSERT_EQUAL_HEX32(USER_OFF, erases[0]);
    assert_platform_untouched();
}

/* Bytes that read back other than written are TFW_ERR_INTERNAL. */
static void test_a_read_back_mismatch_is_internal(void)
{
    uint8_t data = 0x42u;
    uint16_t last = (uint16_t)(TFW_EEPROM_SIZE - 1u);

    /* The model flips the sector's last byte; write the byte that maps there. */
    TEST_ASSERT_EQUAL_UINT32(TFW_RP_FLASH_SECTOR - 1u, last - TFW_BOOT_EE_LEN);
    corrupt_next = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_eeprom_write(last, &data, 1u));
    assert_platform_untouched();
}

static int map(uint32_t base, uint32_t len)
{
    void *w = mmap((void *)(uintptr_t)base, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (w != (void *)(uintptr_t)base) {
        fprintf(stderr, "test_rp2350_eeprom: cannot map 0x%08lx\n", (unsigned long)base);
        return 0;
    }
    return 1;
}

int main(void)
{
    if (!map(WINDOW_BASE, WINDOW_LEN)) {
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_the_model_erases_to_ones_and_programs_only_clear_bits);
    RUN_TEST(test_a_user_address_lands_at_its_byte_of_the_user_sector);
    RUN_TEST(test_a_second_write_keeps_the_rest_of_the_sector);
    RUN_TEST(test_the_last_byte_lands_and_one_past_the_size_is_range);
    RUN_TEST(test_the_boot_area_takes_only_the_confirm_request);
    RUN_TEST(test_a_read_across_the_boot_areas_end_joins_both);
    RUN_TEST(test_a_write_to_the_user_sectors_end_never_erases_the_platform_sector);
    RUN_TEST(test_a_read_back_mismatch_is_internal);
    return UNITY_END();
}
