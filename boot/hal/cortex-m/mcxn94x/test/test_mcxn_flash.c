/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN947's flash writes in tfw-boot (boot/hal/cortex-m/mcxn94x/
 * mcxn_flash.c, row mcxn947), over a model of the boot ROM's flash driver:
 * flash_init fills the config as the driver's header describes it, and
 * flash_erase and flash_program hold each call to the driver's contract (the
 * erase key, a 512-byte program unit, a program only into erased flash)
 * before they act on a RAM flash. The suite checks one sector erased and
 * programmed, a sector that changes nothing left alone, a span outside the
 * flash and a sector off its boundary refused before any ROM call, and a
 * failed ROM call passed up.
 *
 * The ROM's interface is pinned as literals: it is the ROM's, and a value
 * computed from the header would move with it.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "mcxn_flash.h"

#define FLASH_LEN    0x200000u                       /* 2 MiB */
#define FLASH_END    (TFW_MCXN_FLASH_BASE + FLASH_LEN)
#define PLATFORM     0x101FE000u                     /* board_profile.h */
#define ROM_BASE     0x00000000u                     /* the model driver's block base */
#define MAX_CALLS    8u
#define ROM_FAIL     ((int32_t)102)                  /* any status but kStatus_FLASH_Success */

static uint8_t  flash[FLASH_LEN];
static bool     erased[FLASH_LEN / TFW_MCXN_FLASH_SECTOR];
static uint32_t inits;
static uint32_t erases;
static uint32_t programs;
static uint32_t erased_at[MAX_CALLS];
static uint32_t programmed_at[MAX_CALLS];
static const uint8_t *program_src;
static int32_t  init_status;
static int32_t  erase_status;
static int32_t  program_status;
static uint32_t sector_size;

static int32_t model_init(tfw_mcxn_flash_config_t *config)
{
    inits++;
    if (init_status != 0) {
        return init_status;
    }
    config->pflash_block_base  = ROM_BASE;
    config->pflash_total_size  = FLASH_LEN;
    config->pflash_block_count = 1u;
    config->pflash_page_size   = 128u;
    config->pflash_sector_size = sector_size;
    return 0;
}

static int32_t model_erase(tfw_mcxn_flash_config_t *config, uint32_t start,
                           uint32_t length, uint32_t key)
{
    uint32_t off = start - ROM_BASE;

    TEST_ASSERT_EQUAL_HEX32(ROM_BASE, config->pflash_block_base);
    TEST_ASSERT_EQUAL_HEX32(0x6B65666Cu, key);
    TEST_ASSERT_EQUAL_UINT32(0u, off % TFW_MCXN_FLASH_SECTOR);
    TEST_ASSERT_EQUAL_UINT32(TFW_MCXN_FLASH_SECTOR, length);
    TEST_ASSERT_TRUE(off + length <= FLASH_LEN);
    TEST_ASSERT_TRUE(erases < MAX_CALLS);
    erased_at[erases++] = off;
    if (erase_status != 0) {
        return erase_status;
    }
    memset(&flash[off], 0xFF, length);
    erased[off / TFW_MCXN_FLASH_SECTOR] = true;
    return 0;
}

static int32_t model_program(tfw_mcxn_flash_config_t *config, uint32_t start,
                             uint8_t *src, uint32_t length)
{
    uint32_t off = start - ROM_BASE;

    TEST_ASSERT_EQUAL_HEX32(ROM_BASE, config->pflash_block_base);
    TEST_ASSERT_EQUAL_UINT32(0u, off % 512u);
    TEST_ASSERT_EQUAL_UINT32(0u, length % 512u);
    TEST_ASSERT_TRUE(off + length <= FLASH_LEN);
    TEST_ASSERT_TRUE(erased[off / TFW_MCXN_FLASH_SECTOR]);
    TEST_ASSERT_TRUE(programs < MAX_CALLS);
    programmed_at[programs++] = off;
    program_src = src;
    if (program_status != 0) {
        return program_status;
    }
    memcpy(&flash[off], src, length);
    erased[off / TFW_MCXN_FLASH_SECTOR] = false;
    return 0;
}

static const tfw_mcxn_flash_api_t rom = {
    0x00010000u, model_init, model_erase, model_program
};

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < FLASH_LEN; i++) {
        flash[i] = (uint8_t)(i * 7u + 3u);
    }
    memset(erased, 0, sizeof erased);
    memset(erased_at, 0, sizeof erased_at);
    memset(programmed_at, 0, sizeof programmed_at);
    inits          = 0u;
    erases         = 0u;
    programs       = 0u;
    program_src    = NULL;
    init_status    = 0;
    erase_status   = 0;
    program_status = 0;
    sector_size    = 8192u;
}

void tearDown(void) {}

static tfw_status_t update(uint32_t addr, const uint8_t *data, uint32_t len)
{
    return tfw_mcxn_flash_update(&rom, flash, FLASH_END, addr, data, len);
}

static uint8_t at(uint32_t addr)
{
    return flash[addr - TFW_MCXN_FLASH_BASE];
}

/* The platform block, the write tfw-boot makes: one sector, erased once and
 * programmed once at the driver's address for it, its bytes landed and the
 * rest of the sector as it was. */
static void test_a_platform_write_erases_and_programs_one_sector(void)
{
    uint8_t block[192];
    uint8_t before_lo;
    uint8_t before_hi;
    uint32_t i;

    for (i = 0u; i < sizeof block; i++) {
        block[i] = (uint8_t)(0xA0u ^ i);
    }
    before_lo = at(PLATFORM + 0x3Fu);
    before_hi = at(PLATFORM + 0x40u + sizeof block);

    TEST_ASSERT_EQUAL_INT(TFW_OK, update(PLATFORM + 0x40u, block, sizeof block));
    TEST_ASSERT_EQUAL_MEMORY(block, &flash[PLATFORM + 0x40u - TFW_MCXN_FLASH_BASE], sizeof block);
    TEST_ASSERT_EQUAL_HEX8(before_lo, at(PLATFORM + 0x3Fu));
    TEST_ASSERT_EQUAL_HEX8(before_hi, at(PLATFORM + 0x40u + sizeof block));
    TEST_ASSERT_EQUAL_UINT32(1u, inits);
    TEST_ASSERT_EQUAL_UINT32(1u, erases);
    TEST_ASSERT_EQUAL_UINT32(1u, programs);
    TEST_ASSERT_EQUAL_HEX32(0x1FE000u, erased_at[0]);
    TEST_ASSERT_EQUAL_HEX32(0x1FE000u, programmed_at[0]);
}

/* Bytes the flash already holds cost no erase. */
static void test_an_unchanged_write_erases_nothing(void)
{
    uint8_t same[64];

    memcpy(same, &flash[PLATFORM - TFW_MCXN_FLASH_BASE], sizeof same);
    TEST_ASSERT_EQUAL_INT(TFW_OK, update(PLATFORM, same, sizeof same));
    TEST_ASSERT_EQUAL_UINT32(0u, erases);
    TEST_ASSERT_EQUAL_UINT32(0u, programs);
}

/* A span across a sector boundary is both sectors, each once. */
static void test_a_write_across_a_sector_boundary_writes_both(void)
{
    uint8_t  buf[512];
    uint32_t addr = TFW_MCXN_FLASH_BASE + 0xA000u - 100u;

    memset(buf, 0x5A, sizeof buf);
    TEST_ASSERT_EQUAL_INT(TFW_OK, update(addr, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT32(2u, erases);
    TEST_ASSERT_EQUAL_HEX32(0x8000u, erased_at[0]);
    TEST_ASSERT_EQUAL_HEX32(0xA000u, erased_at[1]);
    TEST_ASSERT_EQUAL_MEMORY(buf, &flash[addr - TFW_MCXN_FLASH_BASE], sizeof buf);
}

/* The last byte of the flash is writable; one past it is not. */
static void test_the_last_sector_is_writable(void)
{
    uint8_t b = 0x00u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, update(FLASH_END - 1u, &b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, flash[FLASH_LEN - 1u]);
    TEST_ASSERT_EQUAL_HEX32(FLASH_LEN - TFW_MCXN_FLASH_SECTOR, erased_at[0]);
}

/* Below the flash, past its end, running over its end, and a length that
 * wraps: each refused before the driver is called at all. */
static void test_a_span_outside_the_flash_is_refused(void)
{
    const uint8_t b[2] = { 0u, 0u };

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, update(TFW_MCXN_FLASH_BASE - 1u, b, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, update(0x00000000u, b, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, update(FLASH_END, b, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, update(FLASH_END - 1u, b, 2u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, update(TFW_MCXN_FLASH_BASE + 0x1000u, b, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_UINT32(0u, inits);
    TEST_ASSERT_EQUAL_UINT32(0u, erases);
}

/* A sector write off a sector boundary, even by one program unit, is refused
 * before the driver sees it. */
static void test_a_sector_off_its_boundary_is_refused(void)
{
    static uint8_t buf[TFW_MCXN_FLASH_SECTOR];
    tfw_mcxn_flash_config_t config;

    memset(&config, 0, sizeof config);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_mcxn_flash_sector(&rom, &config, 0x1FE000u + 512u, buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_mcxn_flash_sector(&rom, &config, 1u, buf));
    TEST_ASSERT_EQUAL_UINT32(0u, erases);
    TEST_ASSERT_EQUAL_UINT32(0u, programs);
}

/* A zero-length write is a write of nothing. */
static void test_a_zero_length_write_is_ok(void)
{
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, update(PLATFORM, &b, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, update(FLASH_END, &b, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, erases);
}

/* A failed init, erase or program is the write's failure, and the span stops
 * at the sector it failed on. */
static void test_a_rom_failure_is_passed_up(void)
{
    uint8_t buf[512];

    memset(buf, 0x33, sizeof buf);
    init_status = ROM_FAIL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, update(PLATFORM, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT32(0u, erases);

    setUp();
    erase_status = ROM_FAIL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL,
        update(TFW_MCXN_FLASH_BASE + 0xA000u - 100u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT32(1u, erases);
    TEST_ASSERT_EQUAL_UINT32(0u, programs);

    setUp();
    program_status = ROM_FAIL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL,
        update(TFW_MCXN_FLASH_BASE + 0xA000u - 100u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT32(1u, erases);
    TEST_ASSERT_EQUAL_UINT32(1u, programs);
}

/* A driver that reports a sector the buffer is not sized for is not used. */
static void test_a_driver_of_another_sector_size_is_refused(void)
{
    uint8_t b = 0u;

    sector_size = 4096u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, update(PLATFORM, &b, 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, erases);
}

/* The sector buffer holds the block's bytes during the write and none after
 * it, on success and on failure: the application inherits that SRAM. */
static void test_the_sector_buffer_is_zeroed_after_a_write(void)
{
    uint8_t  block[192];
    uint32_t i;

    memset(block, 0xEE, sizeof block);
    TEST_ASSERT_EQUAL_INT(TFW_OK, update(PLATFORM, block, sizeof block));
    TEST_ASSERT_NOT_NULL(program_src);
    for (i = 0u; i < TFW_MCXN_FLASH_SECTOR; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00u, program_src[i]);
    }
    program_status = ROM_FAIL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, update(PLATFORM + 0x200u, block, sizeof block));
    for (i = 0u; i < TFW_MCXN_FLASH_SECTOR; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00u, program_src[i]);
    }
}

/* The ROM's interface: the tree, the driver's word in it, the erase key, the
 * sector, and the Secure alias the boot addresses the flash by. */
static void test_the_rom_interface_is_pinned(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x1303FC00u, TFW_MCXN_ROM_API_TREE);
    TEST_ASSERT_EQUAL_UINT32(4u, TFW_MCXN_ROM_TREE_FLASH);
    TEST_ASSERT_EQUAL_HEX32(0x6B65666Cu, TFW_MCXN_ROM_ERASE_KEY);
    TEST_ASSERT_EQUAL_UINT32(8192u, TFW_MCXN_FLASH_SECTOR);
    TEST_ASSERT_EQUAL_HEX32(0x10000000u, TFW_MCXN_FLASH_BASE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_platform_write_erases_and_programs_one_sector);
    RUN_TEST(test_an_unchanged_write_erases_nothing);
    RUN_TEST(test_a_write_across_a_sector_boundary_writes_both);
    RUN_TEST(test_the_last_sector_is_writable);
    RUN_TEST(test_a_span_outside_the_flash_is_refused);
    RUN_TEST(test_a_sector_off_its_boundary_is_refused);
    RUN_TEST(test_a_zero_length_write_is_ok);
    RUN_TEST(test_a_rom_failure_is_passed_up);
    RUN_TEST(test_a_driver_of_another_sector_size_is_refused);
    RUN_TEST(test_the_sector_buffer_is_zeroed_after_a_write);
    RUN_TEST(test_the_rom_interface_is_pinned);
    return UNITY_END();
}
