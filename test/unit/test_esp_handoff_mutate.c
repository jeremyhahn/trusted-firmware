/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the application's side of the ESP32 CDI handoff
 * block, src/hal/esp32/hal_handoff.c.
 *
 * The block is the custom reserve of the bootloader's RTC-retained memory,
 * which layer 0 fills and src/core/handoff.c reads. This holds the pointer
 * to exactly that reserve: its first byte, every byte of a seeded block
 * written through it landing in the reserve, ESP-IDF's own record on either
 * side untouched, and the same address on every call.
 *
 * Deterministic: xorshift32 from a literal seed.
 */

#include <string.h>
#include "unity.h"
#include "bootloader_common.h"
#include "tfw/hal.h"
#include "tfw/boot.h"

static rtc_retain_mem_t mem;
static unsigned         gets;
static uint32_t         rng;

rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void)
{
    gets++;
    return &mem;
}

static uint8_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (uint8_t)rng;
}

void setUp(void)
{
    memset(&mem, 0xC3, sizeof mem);
    gets = 0u;
    rng  = 0x2545F491u;
}

void tearDown(void) {}

static void test_the_block_is_the_custom_reserve(void)
{
    TEST_ASSERT_EQUAL_PTR(&mem.custom[0], tfw_hal_handoff_block());
    TEST_ASSERT_EQUAL_UINT(1u, gets);
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_HANDOFF_LEN, sizeof mem.custom);
}

static void test_a_block_written_through_it_lands_in_the_reserve_only(void)
{
    uint8_t  want[TFW_BOOT_HANDOFF_LEN];
    uint8_t  around[sizeof mem.before];
    uint8_t *p = tfw_hal_handoff_block();
    unsigned i;

    for (i = 0u; i < sizeof want; i++) {
        want[i] = rnd();
    }
    memcpy(p, want, sizeof want);
    TEST_ASSERT_EQUAL_MEMORY(want, mem.custom, sizeof want);
    memset(around, 0xC3, sizeof around);
    TEST_ASSERT_EQUAL_MEMORY(around, mem.before, sizeof mem.before);
    TEST_ASSERT_EQUAL_MEMORY(around, mem.after, sizeof mem.after);
}

static void test_every_call_names_the_same_block(void)
{
    uint8_t *first = tfw_hal_handoff_block();

    TEST_ASSERT_EQUAL_PTR(first, tfw_hal_handoff_block());
    TEST_ASSERT_EQUAL_PTR(first, tfw_hal_handoff_block());
    TEST_ASSERT_EQUAL_UINT(3u, gets);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_block_is_the_custom_reserve);
    RUN_TEST(test_a_block_written_through_it_lands_in_the_reserve_only);
    RUN_TEST(test_every_call_names_the_same_block);
    return UNITY_END();
}
