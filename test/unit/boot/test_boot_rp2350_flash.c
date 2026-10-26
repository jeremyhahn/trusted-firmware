/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350's flash writes in tfw-boot (boot/hal/cortex-m/rp2350/rp_flash.c,
 * row rp2350). Every platform-block and image write goes through one sector
 * read-modify-write; this suite supplies the two sector functions over a RAM
 * flash, in place of rp_flash_rom.c's boot ROM calls, and checks that a write
 * changes the bytes asked for and no others, erases no sector it need not,
 * refuses an address outside the flash, and leaves no copy of the bytes in
 * its buffer.
 *
 * The ROM table codes are pinned as literals: they are the ROM's interface,
 * and a code computed from TFW_RP_ROM_CODE would move with it.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "rp_flash.h"

#define FLASH_LEN    0x400000u                      /* 4 MiB, the Pico 2 W's */
#define FLASH_END    (TFW_RP_XIP_BASE + FLASH_LEN)
#define PLATFORM     0x103FF000u                    /* board_profile.h */
#define MAX_WRITES   8u

static uint8_t        flash[FLASH_LEN];
static uint32_t       writes;
static uint32_t       written[MAX_WRITES];
static const uint8_t *write_buf;
static tfw_status_t   write_status;

void tfw_rp_flash_sector_read(uint32_t off, uint8_t *buf)
{
    TEST_ASSERT_EQUAL_UINT32(0u, off % TFW_RP_FLASH_SECTOR);
    TEST_ASSERT_TRUE(off + TFW_RP_FLASH_SECTOR <= FLASH_LEN);
    memcpy(buf, &flash[off], TFW_RP_FLASH_SECTOR);
}

tfw_status_t tfw_rp_flash_sector_write(uint32_t off, const uint8_t *buf)
{
    TEST_ASSERT_EQUAL_UINT32(0u, off % TFW_RP_FLASH_SECTOR);
    TEST_ASSERT_TRUE(off + TFW_RP_FLASH_SECTOR <= FLASH_LEN);
    TEST_ASSERT_TRUE(writes < MAX_WRITES);
    written[writes++] = off;
    write_buf = buf;
    if (write_status != TFW_OK) {
        return write_status;
    }
    memcpy(&flash[off], buf, TFW_RP_FLASH_SECTOR);
    return TFW_OK;
}

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < FLASH_LEN; i++) {
        flash[i] = (uint8_t)(i * 13u + 5u);
    }
    writes       = 0u;
    write_buf    = NULL;
    write_status = TFW_OK;
    memset(written, 0, sizeof written);
}

void tearDown(void) {}

static uint8_t at(uint32_t addr)
{
    return flash[addr - TFW_RP_XIP_BASE];
}

/* The platform block, the write tfw-boot makes: its bytes land, the rest of
 * its sector keeps what it held, and the one sector is written once. */
static void test_a_platform_write_changes_its_bytes_and_no_others(void)
{
    uint8_t  block[192];
    uint8_t  before_lo;
    uint8_t  before_hi;
    uint32_t i;

    for (i = 0u; i < sizeof block; i++) {
        block[i] = (uint8_t)(0xC0u ^ i);
    }
    before_lo = at(PLATFORM + 0x3Fu);
    before_hi = at(PLATFORM + 0x40u + sizeof block);

    TEST_ASSERT_EQUAL_INT(TFW_OK,
        tfw_rp_flash_update(PLATFORM + 0x40u, block, sizeof block, FLASH_END));
    TEST_ASSERT_EQUAL_MEMORY(block, &flash[PLATFORM + 0x40u - TFW_RP_XIP_BASE], sizeof block);
    TEST_ASSERT_EQUAL_HEX8(before_lo, at(PLATFORM + 0x3Fu));
    TEST_ASSERT_EQUAL_HEX8(before_hi, at(PLATFORM + 0x40u + sizeof block));
    TEST_ASSERT_EQUAL_UINT32(1u, writes);
    TEST_ASSERT_EQUAL_HEX32(0x3FF000u, written[0]);
}

/* Bytes the flash already holds cost no erase. */
static void test_an_unchanged_write_erases_nothing(void)
{
    uint8_t same[64];

    memcpy(same, &flash[PLATFORM - TFW_RP_XIP_BASE], sizeof same);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_update(PLATFORM, same, sizeof same, FLASH_END));
    TEST_ASSERT_EQUAL_UINT32(0u, writes);
}

/* A span across a sector boundary is two sectors, each written once, and
 * the bytes on both sides of the boundary land. */
static void test_a_write_across_a_sector_boundary_writes_both(void)
{
    uint8_t  page[TFW_RP_FLASH_PAGE];
    uint32_t addr = TFW_RP_XIP_BASE + 0x9000u - 100u;

    memset(page, 0x5A, sizeof page);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_update(addr, page, sizeof page, FLASH_END));
    TEST_ASSERT_EQUAL_UINT32(2u, writes);
    TEST_ASSERT_EQUAL_HEX32(0x8000u, written[0]);
    TEST_ASSERT_EQUAL_HEX32(0x9000u, written[1]);
    TEST_ASSERT_EQUAL_MEMORY(page, &flash[addr - TFW_RP_XIP_BASE], sizeof page);
}

/* The last byte of the flash is writable; one past it is not. */
static void test_the_last_sector_is_writable(void)
{
    uint8_t b = 0x00u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_update(FLASH_END - 1u, &b, 1u, FLASH_END));
    TEST_ASSERT_EQUAL_HEX8(0x00u, flash[FLASH_LEN - 1u]);
    TEST_ASSERT_EQUAL_UINT32(1u, writes);
    TEST_ASSERT_EQUAL_HEX32(FLASH_LEN - TFW_RP_FLASH_SECTOR, written[0]);
}

/* Below the window, past its end, running over its end, and a length that
 * wraps: each refused before a sector is read or written. */
static void test_an_address_outside_the_flash_is_refused(void)
{
    uint8_t b[2] = { 0u, 0u };

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
        tfw_rp_flash_update(TFW_RP_XIP_BASE - 1u, b, 1u, FLASH_END));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
        tfw_rp_flash_update(0x00000000u, b, 1u, FLASH_END));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
        tfw_rp_flash_update(FLASH_END, b, 1u, FLASH_END));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
        tfw_rp_flash_update(FLASH_END - 1u, b, 2u, FLASH_END));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
        tfw_rp_flash_update(TFW_RP_XIP_BASE + 0x1000u, b, 0xFFFFFFFFu, FLASH_END));
    TEST_ASSERT_EQUAL_UINT32(0u, writes);
}

/* A zero-length write is a write of nothing. */
static void test_a_zero_length_write_is_ok(void)
{
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_update(PLATFORM, &b, 0u, FLASH_END));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_update(FLASH_END, &b, 0u, FLASH_END));
    TEST_ASSERT_EQUAL_UINT32(0u, writes);
}

/* A sector the ROM could not write stops the span there and says so. */
static void test_a_failed_sector_write_stops_and_is_returned(void)
{
    uint8_t page[TFW_RP_FLASH_PAGE];

    memset(page, 0x33, sizeof page);
    write_status = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
        tfw_rp_flash_update(TFW_RP_XIP_BASE + 0x9000u - 100u, page, sizeof page, FLASH_END));
    TEST_ASSERT_EQUAL_UINT32(1u, writes);
}

/* The sector buffer holds the platform block's bytes during the write and
 * none after it: the application inherits that SRAM. */
static void test_the_sector_buffer_is_zeroed_after_a_write(void)
{
    uint8_t  block[192];
    uint32_t i;

    memset(block, 0xEE, sizeof block);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_update(PLATFORM, block, sizeof block, FLASH_END));
    TEST_ASSERT_NOT_NULL(write_buf);
    for (i = 0u; i < TFW_RP_FLASH_SECTOR; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00u, write_buf[i]);
    }

    write_status = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
        tfw_rp_flash_update(PLATFORM + 0x200u, block, sizeof block, FLASH_END));
    for (i = 0u; i < TFW_RP_FLASH_SECTOR; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00u, write_buf[i]);
    }
}

/* The ROM's interface: the lookup's halfword pointer, the Secure Arm flag,
 * the six codes, and the sector erase. */
static void test_the_rom_interface_is_pinned(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000016u, TFW_RP_ROM_LOOKUP_PTR);
    TEST_ASSERT_EQUAL_HEX32(0x0004u, TFW_RP_RT_FLAG_FUNC_ARM_SEC);
    TEST_ASSERT_EQUAL_HEX32(0x4649u, TFW_RP_ROM_CONNECT_INTERNAL_FLASH);
    TEST_ASSERT_EQUAL_HEX32(0x5845u, TFW_RP_ROM_FLASH_EXIT_XIP);
    TEST_ASSERT_EQUAL_HEX32(0x4552u, TFW_RP_ROM_FLASH_RANGE_ERASE);
    TEST_ASSERT_EQUAL_HEX32(0x5052u, TFW_RP_ROM_FLASH_RANGE_PROGRAM);
    TEST_ASSERT_EQUAL_HEX32(0x4346u, TFW_RP_ROM_FLASH_FLUSH_CACHE);
    TEST_ASSERT_EQUAL_HEX32(0x5843u, TFW_RP_ROM_FLASH_ENTER_CMD_XIP);
    TEST_ASSERT_EQUAL_UINT32(4096u, TFW_RP_FLASH_SECTOR);
    TEST_ASSERT_EQUAL_UINT32(256u, TFW_RP_FLASH_PAGE);
    TEST_ASSERT_EQUAL_HEX32(0x20u, TFW_RP_FLASH_SECTOR_ERASE_CMD);
    TEST_ASSERT_EQUAL_HEX32(0x10000000u, TFW_RP_XIP_BASE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_platform_write_changes_its_bytes_and_no_others);
    RUN_TEST(test_an_unchanged_write_erases_nothing);
    RUN_TEST(test_a_write_across_a_sector_boundary_writes_both);
    RUN_TEST(test_the_last_sector_is_writable);
    RUN_TEST(test_an_address_outside_the_flash_is_refused);
    RUN_TEST(test_a_zero_length_write_is_ok);
    RUN_TEST(test_a_failed_sector_write_stops_and_is_returned);
    RUN_TEST(test_the_sector_buffer_is_zeroed_after_a_write);
    RUN_TEST(test_the_rom_interface_is_pinned);
    return UNITY_END();
}
