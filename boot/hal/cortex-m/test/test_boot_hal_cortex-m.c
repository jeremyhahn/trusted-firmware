/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/hal/cortex-m/boot_hal_cortex-m.c on the host (#628), under the MPS2
 * AN505's profile: the page write's range checks, the platform block's bounds
 * and where its write lands, the handoff block, and the reset cause's order
 * (the factory copy before the latch is read). boot_hal_cortex-m_stubs.c
 * stands in for the SoC's flash write and logs it. The jump is boot_jump.c's
 * and is not built here: its msr and bx run only on the core, and
 * integration-test-cortex-m boots the image that links it.
 *
 * Here and not in test/unit/boot/: the stubs name tfw_cm_*, which
 * lint-arch-leak allows only under the Cortex-M HAL directories.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_hal.h"
#include "boot_slots.h"

#define PAGE ((uint32_t)TFW_OTA_PAGE_MAX)

extern uint32_t       cm_stub_write_addr[];
extern uint32_t       cm_stub_write_len[];
extern const uint8_t *cm_stub_write_data[];
extern unsigned       cm_stub_nwrite;
extern tfw_status_t   cm_stub_write_status;

extern char              cm_stub_order[];
extern unsigned          cm_stub_norder;
extern tfw_reset_cause_t cm_stub_cause;

static uint8_t page[TFW_OTA_PAGE_MAX];

void setUp(void)
{
    cm_stub_nwrite = 0;
    cm_stub_write_status = TFW_OK;
    memset(cm_stub_order, 0, 8);
    cm_stub_norder = 0;
    cm_stub_cause = TFW_RESET_UNKNOWN;
}

void tearDown(void) {}

void test_write_page_below_app_region_is_protected(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, (uint32_t)TFW_APP_REGION_BASE % PAGE);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION,
                      tfw_boot_flash_write_page((uint32_t)TFW_APP_REGION_BASE - PAGE, page));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_boot_flash_write_page(0u, page));
    TEST_ASSERT_EQUAL_UINT(0u, cm_stub_nwrite);
}

void test_write_page_past_the_write_end_is_protected(void)
{
    uint32_t last = ((uint32_t)TFW_APP_WRITE_END / PAGE) * PAGE;

    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_boot_flash_write_page(last, page));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_boot_flash_write_page(last + PAGE, page));
    TEST_ASSERT_EQUAL_UINT(0u, cm_stub_nwrite);
}

/* The address space's last page ends at a wrapped 0, which addr plus a page
 * would compare as below any end (row/628-cortex-m). */
void test_write_page_refuses_a_page_whose_end_wraps(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_boot_flash_write_page((uint32_t)(0u - PAGE), page));
    TEST_ASSERT_EQUAL_UINT(0u, cm_stub_nwrite);
}

void test_page_size_is_the_ota_page(void)
{
    TEST_ASSERT_EQUAL_UINT16(TFW_OTA_PAGE_MAX, tfw_boot_flash_page_size());
}

void test_write_page_misaligned_is_out_of_range(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_flash_write_page((uint32_t)TFW_APP_REGION_BASE + 1u, page));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      tfw_boot_flash_write_page((uint32_t)TFW_APP_REGION_BASE + PAGE / 2u, page));
    TEST_ASSERT_EQUAL_UINT(0u, cm_stub_nwrite);
}

void test_write_page_in_range_writes_the_page_once(void)
{
    uint32_t first = (uint32_t)TFW_APP_REGION_BASE;
    uint32_t last  = ((uint32_t)TFW_APP_WRITE_END / PAGE) * PAGE - PAGE;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_flash_write_page(first, page));
    TEST_ASSERT_EQUAL_UINT(1u, cm_stub_nwrite);
    TEST_ASSERT_EQUAL_HEX32(first, cm_stub_write_addr[0]);
    TEST_ASSERT_EQUAL_UINT32(PAGE, cm_stub_write_len[0]);
    TEST_ASSERT_EQUAL_PTR(page, cm_stub_write_data[0]);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_flash_write_page(last, page));
    TEST_ASSERT_EQUAL_UINT(2u, cm_stub_nwrite);
    TEST_ASSERT_EQUAL_HEX32(last, cm_stub_write_addr[1]);
}

void test_write_page_returns_the_socs_status(void)
{
    cm_stub_write_status = TFW_ERR_CRC;
    TEST_ASSERT_EQUAL(TFW_ERR_CRC, tfw_boot_flash_write_page((uint32_t)TFW_APP_REGION_BASE, page));
    TEST_ASSERT_EQUAL_UINT(1u, cm_stub_nwrite);
}

void test_nvm_bounds_are_the_platform_size(void)
{
    uint8_t buf[4] = {1, 2, 3, 4};
    uint16_t end = (uint16_t)TFW_CM_PLATFORM_SIZE;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_read((uint16_t)(end - 3u), buf, 4));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_read(end, buf, 1));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_write((uint16_t)(end - 3u), buf, 4));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_write(end, buf, 1));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_write(0xFFFFu, buf, 4));
    TEST_ASSERT_EQUAL_UINT(0u, cm_stub_nwrite);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_write((uint16_t)(end - 4u), buf, 4));
    TEST_ASSERT_EQUAL_UINT(1u, cm_stub_nwrite);
}

void test_nvm_write_lands_at_the_platform_base(void)
{
    uint8_t buf[3] = {0xA5, 0x5A, 0x01};

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_write(0x10u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT(1u, cm_stub_nwrite);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)TFW_CM_PLATFORM_BASE + 0x10u, cm_stub_write_addr[0]);
    TEST_ASSERT_EQUAL_UINT32(sizeof buf, cm_stub_write_len[0]);
    TEST_ASSERT_EQUAL_PTR(buf, cm_stub_write_data[0]);
}

void test_handoff_round_trips(void)
{
    uint8_t out[TFW_BOOT_HANDOFF_LEN], in[TFW_BOOT_HANDOFF_LEN];

    for (unsigned i = 0; i < sizeof out; i++) {
        out[i] = (uint8_t)(i * 7u + 3u);
    }
    tfw_boot_handoff_write(out, (uint8_t)sizeof out);
    memset(in, 0, sizeof in);
    tfw_boot_handoff_read(in, (uint8_t)sizeof in);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out, in, sizeof out);
    TEST_ASSERT_EQUAL_UINT(0u, cm_stub_nwrite);
}

void test_reset_cause_runs_the_factory_copy_first(void)
{
    cm_stub_cause = TFW_RESET_WATCHDOG;
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_boot_reset_cause());
    TEST_ASSERT_EQUAL_STRING("FC", cm_stub_order);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_page_below_app_region_is_protected);
    RUN_TEST(test_write_page_past_the_write_end_is_protected);
    RUN_TEST(test_write_page_refuses_a_page_whose_end_wraps);
    RUN_TEST(test_page_size_is_the_ota_page);
    RUN_TEST(test_write_page_misaligned_is_out_of_range);
    RUN_TEST(test_write_page_in_range_writes_the_page_once);
    RUN_TEST(test_write_page_returns_the_socs_status);
    RUN_TEST(test_nvm_bounds_are_the_platform_size);
    RUN_TEST(test_nvm_write_lands_at_the_platform_base);
    RUN_TEST(test_handoff_round_trips);
    RUN_TEST(test_reset_cause_runs_the_factory_copy_first);
    return UNITY_END();
}
