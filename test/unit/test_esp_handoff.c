/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/esp32/hal_handoff.c on the host, over the bootloader's RTC-retained
 * block in test/mock/esp_sys_mock.c: the handoff block is the block's custom
 * reserve, in place -- what the bootloader wrote there is what the
 * application reads, and what the application clears is cleared there. The
 * reserve's size is the handoff block's, or hal_handoff.c does not compile.
 */
#include <string.h>

#include "unity.h"
#include "esp_sys_mock.h"
#include "tfw/boot.h"
#include "tfw/hal.h"

void setUp(void)
{
    mock_sys_reset();
}

void tearDown(void) {}

static void test_the_handoff_block_is_the_rtc_reserve_in_place(void)
{
    TEST_ASSERT_EQUAL_PTR(mock_sys.retain.custom, tfw_hal_handoff_block());
    TEST_ASSERT_EQUAL_UINT(1u, mock_sys.retain_calls);
}

static void test_what_the_bootloader_wrote_is_what_the_application_reads(void)
{
    for (unsigned i = 0u; i < TFW_BOOT_HANDOFF_LEN; i++) {
        mock_sys.retain.custom[i] = (uint8_t)(0xA5u ^ i);
    }
    const uint8_t *block = tfw_hal_handoff_block();
    for (unsigned i = 0u; i < TFW_BOOT_HANDOFF_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA5u ^ i), block[i]);
    }
}

static void test_a_write_through_the_block_lands_in_the_reserve_and_nowhere_else(void)
{
    memset(&mock_sys.retain, 0x5A, sizeof mock_sys.retain);
    memset(tfw_hal_handoff_block(), 0, TFW_BOOT_HANDOFF_LEN);
    for (unsigned i = 0u; i < TFW_BOOT_HANDOFF_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(0u, mock_sys.retain.custom[i]);
    }
    TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5Au, mock_sys.retain.crc);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, mock_sys.retain.reserve);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_handoff_block_is_the_rtc_reserve_in_place);
    RUN_TEST(test_what_the_bootloader_wrote_is_what_the_application_reads);
    RUN_TEST(test_a_write_through_the_block_lands_in_the_reserve_and_nowhere_else);
    return UNITY_END();
}
