/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Pico 2 W's flash map (row rp2350), pinned by literal against
 * boards/rp2350-pico2w/board_profile.h. The flash is 4 MiB: pico-sdk 2.1.1
 * (bddd20f928ce76142793bef434d4f75f4af6e433),
 * src/boards/include/boards/pico2_w.h line 79, PICO_FLASH_SIZE_BYTES
 * (4 * 1024 * 1024), and the Pico 2 W datasheet's §1. A change to any region
 * here moves where tfw-boot writes the platform block on the board.
 */
#include <stdint.h>
#include "unity.h"
#include "tfw/config.h"
#include "rp_flash.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_flash_is_4_mib_from_the_xip_base(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x10000000u, TFW_RP_XIP_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x10400000u, (uint32_t)TFW_FLASH_TOTAL);
    TEST_ASSERT_EQUAL_HEX32(0x00400000u, (uint32_t)TFW_FLASH_TOTAL - TFW_RP_XIP_BASE);
}

static void test_tfw_boot_takes_the_first_32_kib(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x10000000u, (uint32_t)TFW_BOOT_REGION_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x00008000u, (uint32_t)TFW_BOOT_REGION_SIZE);
}

static void test_the_application_follows_tfw_boot(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x10008000u, (uint32_t)TFW_APP_REGION_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x00020000u, (uint32_t)TFW_APP_REGION_SIZE);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(TFW_BOOT_REGION_BASE + TFW_BOOT_REGION_SIZE),
                            (uint32_t)TFW_APP_REGION_BASE);
}

/* The platform block is the last 4 KiB sector: it ends at the flash's end,
 * starts on a sector boundary, and lies above the application. */
static void test_the_platform_block_is_the_last_sector(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x103FF000u, (uint32_t)TFW_CM_PLATFORM_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x00001000u, (uint32_t)TFW_CM_PLATFORM_SIZE);
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FLASH_SECTOR, (uint32_t)TFW_CM_PLATFORM_SIZE);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)TFW_FLASH_TOTAL,
                            (uint32_t)(TFW_CM_PLATFORM_BASE + TFW_CM_PLATFORM_SIZE));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)TFW_CM_PLATFORM_BASE % TFW_RP_FLASH_SECTOR);
    TEST_ASSERT_TRUE((uint32_t)(TFW_APP_REGION_BASE + TFW_APP_REGION_SIZE) <=
                     (uint32_t)TFW_CM_PLATFORM_BASE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_flash_is_4_mib_from_the_xip_base);
    RUN_TEST(test_tfw_boot_takes_the_first_32_kib);
    RUN_TEST(test_the_application_follows_tfw_boot);
    RUN_TEST(test_the_platform_block_is_the_last_sector);
    return UNITY_END();
}
