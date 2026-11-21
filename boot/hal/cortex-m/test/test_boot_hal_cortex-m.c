/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The core's half of tfw-boot's Cortex-M HAL (boot/hal/cortex-m/
 * boot_hal_cortex-m.c) on the host, built once under each MPS2 profile on its
 * one-slot geometry (#628). The SoC's half and the shared early HAL are
 * stand-ins here that record what they were asked: the part's program
 * sequence (tfw_cm_boot_flash_write), the factory copies, the reset cause,
 * the system reset and the watchdog. The jump to the application needs the
 * core and is boot_jump.c's, which this test does not link.
 *
 * What it pins: a page write lands inside the application region and nowhere
 * else, tfw-boot's own region and an address whose page end wraps included;
 * the platform block's range; the handoff block round trip; and the factory
 * copies going in before the reset cause is read.
 */
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_hal.h"
#include "boot_soc.h"
#include "hal_early.h"

#define PAGE      ((uint32_t)TFW_OTA_PAGE_MAX)
#define APP_BASE  ((uint32_t)TFW_APP_REGION_BASE)
#define APP_END   ((uint32_t)TFW_APP_WRITE_END)
#define PLAT_BASE ((uint32_t)TFW_CM_PLATFORM_BASE)
#define PLAT_SIZE ((uint32_t)TFW_CM_PLATFORM_SIZE)

/* ---- the stand-ins, each call numbered in the order it came */
static uint32_t     calls;
static uint32_t     write_calls;
static uint32_t     write_addr;
static const uint8_t *write_data;
static uint32_t     write_len;
static tfw_status_t write_ret;
static uint32_t     factory_at;
static uint32_t     cause_at;
static uint32_t     wdog_at;
static jmp_buf      reset_jmp;

tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    write_calls++;
    write_addr = addr;
    write_data = data;
    write_len = len;
    return write_ret;
}

void tfw_cm_boot_factory(void)
{
    factory_at = ++calls;
}

tfw_reset_cause_t tfw_hal_early_reset_cause(void)
{
    cause_at = ++calls;
    return TFW_RESET_WATCHDOG;
}

_Noreturn void tfw_cm_system_reset(void)
{
    longjmp(reset_jmp, 1);
}

void tfw_cm_soc_watchdog_arm(void)
{
    wdog_at = ++calls;
}

void setUp(void)
{
    calls = 0u;
    write_calls = 0u;
    write_addr = 0u;
    write_data = NULL;
    write_len = 0u;
    write_ret = TFW_OK;
    factory_at = 0u;
    cause_at = 0u;
    wdog_at = 0u;
}

void tearDown(void) {}

static uint8_t page[TFW_OTA_PAGE_MAX];

static void test_page_size_is_the_ota_page(void)
{
    TEST_ASSERT_EQUAL_UINT16(TFW_OTA_PAGE_MAX, tfw_boot_flash_page_size());
}

/* The first and the last page of the region go to the part, a whole page. */
static void test_write_page_inside_the_region_reaches_the_part(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_flash_write_page(APP_BASE, page));
    TEST_ASSERT_EQUAL_UINT32(1u, write_calls);
    TEST_ASSERT_EQUAL_HEX32(APP_BASE, write_addr);
    TEST_ASSERT_EQUAL_PTR(page, write_data);
    TEST_ASSERT_EQUAL_UINT32(PAGE, write_len);

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_flash_write_page(APP_END - PAGE, page));
    TEST_ASSERT_EQUAL_UINT32(2u, write_calls);
    TEST_ASSERT_EQUAL_HEX32(APP_END - PAGE, write_addr);
}

/* The part's refusal comes back as it was given. */
static void test_write_page_returns_the_parts_status(void)
{
    write_ret = TFW_ERR_CRC;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, tfw_boot_flash_write_page(APP_BASE, page));
}

/* Refusal: an address off a page boundary. */
static void test_write_page_refuses_an_unaligned_address(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_flash_write_page(APP_BASE + 4u, page));
    TEST_ASSERT_EQUAL_UINT32(0u, write_calls);
}

/* Refusal: the page below the region, tfw-boot's own on these boards. */
static void test_write_page_refuses_below_the_region(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_flash_write_page(APP_BASE - PAGE, page));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_flash_write_page(0u, page));
    TEST_ASSERT_EQUAL_UINT32(0u, write_calls);
}

/* Refusal: the page one past the region's end. */
static void test_write_page_refuses_past_the_end(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_flash_write_page(APP_END, page));
    TEST_ASSERT_EQUAL_UINT32(0u, write_calls);
}

/* Refusal: the last page of the address space, whose end wraps to 0 and so
 * sits under any end the region has. */
static void test_write_page_refuses_a_page_whose_end_wraps(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_boot_flash_write_page((uint32_t)(0u - PAGE), page));
    TEST_ASSERT_EQUAL_UINT32(0u, write_calls);
}

/* The platform block's write: at its base plus the offset, up to its last
 * byte. */
static void test_nvm_write_inside_the_block_reaches_the_part(void)
{
    uint8_t b[16] = {0};

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_nvm_write(0x10u, b, (uint8_t)sizeof(b)));
    TEST_ASSERT_EQUAL_HEX32(PLAT_BASE + 0x10u, write_addr);
    TEST_ASSERT_EQUAL_PTR(b, write_data);
    TEST_ASSERT_EQUAL_UINT32(sizeof(b), write_len);

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_nvm_write((uint16_t)(PLAT_SIZE - sizeof(b)), b,
                                                     (uint8_t)sizeof(b)));
    TEST_ASSERT_EQUAL_HEX32(PLAT_BASE + PLAT_SIZE - sizeof(b), write_addr);
    TEST_ASSERT_EQUAL_UINT32(2u, write_calls);
}

/* Refusal: a write one byte past the block, and one wholly past it. */
static void test_nvm_write_refuses_past_the_block(void)
{
    uint8_t b[16] = {0};

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_nvm_write((uint16_t)(PLAT_SIZE - sizeof(b) + 1u), b,
                                                            (uint8_t)sizeof(b)));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_nvm_write(0xFFFFu, b, 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, write_calls);
}

/* Refusal: a read past the block leaves the buffer alone. (A read inside it
 * is a copy from the part's address, which the host does not map.) */
static void test_nvm_read_refuses_past_the_block(void)
{
    uint8_t b[4] = {0xA5u, 0xA5u, 0xA5u, 0xA5u};

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_nvm_read((uint16_t)(PLAT_SIZE - 3u), b, 4u));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, b[3]);
}

/* The handoff block reads back as it was written, every byte. */
static void test_handoff_reads_back_what_was_written(void)
{
    uint8_t out[TFW_BOOT_HANDOFF_LEN];
    uint8_t in[TFW_BOOT_HANDOFF_LEN];
    uint32_t i;

    for (i = 0u; i < sizeof(out); i++) {
        out[i] = (uint8_t)(i * 7u + 1u);
    }
    memset(in, 0, sizeof(in));
    tfw_boot_handoff_write(out, (uint8_t)sizeof(out));
    tfw_boot_handoff_read(in, (uint8_t)sizeof(in));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(out, in, sizeof(out));
}

/* The factory copies go in before the cause is read, and the cause is the
 * early HAL's. */
static void test_reset_cause_runs_the_factory_copies_first(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_RESET_WATCHDOG, tfw_boot_reset_cause());
    TEST_ASSERT_EQUAL_UINT32(1u, factory_at);
    TEST_ASSERT_EQUAL_UINT32(2u, cause_at);
}

static void test_reset_asks_for_a_system_reset(void)
{
    if (setjmp(reset_jmp) == 0) {
        tfw_boot_reset();
        TEST_FAIL_MESSAGE("tfw_boot_reset returned");
    }
}

static void test_watchdog_arm_arms_the_socs(void)
{
    tfw_boot_watchdog_arm();

    TEST_ASSERT_EQUAL_UINT32(1u, wdog_at);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_page_size_is_the_ota_page);
    RUN_TEST(test_write_page_inside_the_region_reaches_the_part);
    RUN_TEST(test_write_page_returns_the_parts_status);
    RUN_TEST(test_write_page_refuses_an_unaligned_address);
    RUN_TEST(test_write_page_refuses_below_the_region);
    RUN_TEST(test_write_page_refuses_past_the_end);
    RUN_TEST(test_write_page_refuses_a_page_whose_end_wraps);
    RUN_TEST(test_nvm_write_inside_the_block_reaches_the_part);
    RUN_TEST(test_nvm_write_refuses_past_the_block);
    RUN_TEST(test_nvm_read_refuses_past_the_block);
    RUN_TEST(test_handoff_reads_back_what_was_written);
    RUN_TEST(test_reset_cause_runs_the_factory_copies_first);
    RUN_TEST(test_reset_asks_for_a_system_reset);
    RUN_TEST(test_watchdog_arm_arms_the_socs);
    return UNITY_END();
}
