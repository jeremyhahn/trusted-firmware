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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_page_below_app_region_is_protected);
    return UNITY_END();
}
