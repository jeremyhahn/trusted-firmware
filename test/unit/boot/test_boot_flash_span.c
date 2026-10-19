/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 boot HAL's aligned read windows (boot/hal/esp32/flash_span.c,
 * #48). bootloader_flash_read refuses a source, length or destination that is
 * not 4-byte aligned; layer 0 asked it for the SUIT envelope's 2-byte length
 * after the trailer and booted nothing. Every window here is one that read
 * may be given, and the bytes copied out of the windows are the bytes asked
 * for, at every offset and length the contract can pass.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "flash_span.h"

#define FLASH_LEN 1024u

static uint8_t flash[FLASH_LEN];

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < FLASH_LEN; i++) {
        flash[i] = (uint8_t)(i * 7u + 3u);
    }
}

void tearDown(void) {}

/* As boot_hal_esp32.c reads: window by window, each one checked against
 * what bootloader_flash_read accepts, then copied out. */
static void read_through(uint32_t addr, uint8_t *out, uint32_t want, uint32_t cap)
{
    tfw_flash_span_t s;
    uint32_t         windows = 0u;

    while (want > 0u) {
        tfw_flash_span(addr, want, cap, &s);
        TEST_ASSERT_EQUAL_UINT32(0u, s.from % 4u);
        TEST_ASSERT_EQUAL_UINT32(0u, s.len % 4u);
        TEST_ASSERT_TRUE(s.len <= cap);
        TEST_ASSERT_TRUE(s.skip < 4u);
        TEST_ASSERT_EQUAL_UINT32(addr, s.from + s.skip);
        TEST_ASSERT_TRUE(s.take >= 1u && s.take <= want);
        TEST_ASSERT_TRUE(s.skip + s.take <= s.len);
        TEST_ASSERT_TRUE(s.len - (s.skip + s.take) < 4u);
        TEST_ASSERT_TRUE(s.from + s.len <= FLASH_LEN);
        memcpy(out, &flash[s.from + s.skip], s.take);
        addr += s.take;
        out  += s.take;
        want -= s.take;
        windows++;
        TEST_ASSERT_TRUE(windows <= FLASH_LEN);
    }
}

/* The read that fired: the envelope length at the trailer's end, then the
 * envelope from two bytes past it. */
static void test_the_suit_length_and_envelope_reads_are_aligned(void)
{
    tfw_flash_span_t s;

    tfw_flash_span(0x90090u, 2u, 256u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x90090u, s.from);
    TEST_ASSERT_EQUAL_UINT32(4u, s.len);
    TEST_ASSERT_EQUAL_UINT32(0u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(2u, s.take);

    tfw_flash_span(0x90092u, 128u, 256u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x90090u, s.from);
    TEST_ASSERT_EQUAL_UINT32(132u, s.len);
    TEST_ASSERT_EQUAL_UINT32(2u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(128u, s.take);
}

/* An aligned read is one window of exactly itself. */
static void test_an_aligned_read_is_its_own_window(void)
{
    tfw_flash_span_t s;

    tfw_flash_span(0x20000u, 72u, 256u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x20000u, s.from);
    TEST_ASSERT_EQUAL_UINT32(72u, s.len);
    TEST_ASSERT_EQUAL_UINT32(0u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(72u, s.take);
}

/* Longer than the bounce buffer: the first window fills it, no more. */
static void test_a_read_past_the_buffer_is_chunked(void)
{
    tfw_flash_span_t s;

    tfw_flash_span(0x1001u, 4096u, 256u, &s);
    TEST_ASSERT_EQUAL_HEX32(0x1000u, s.from);
    TEST_ASSERT_EQUAL_UINT32(256u, s.len);
    TEST_ASSERT_EQUAL_UINT32(1u, s.skip);
    TEST_ASSERT_EQUAL_UINT32(255u, s.take);
}

/* Every offset and length the contract's uint8_t length allows, through the
 * HAL's buffer and through the smallest one the header permits: the bytes
 * out are the bytes asked for, and nothing around them is written. */
static void test_every_offset_and_length_reads_its_bytes(void)
{
    static const uint32_t caps[] = { 256u, 8u };
    uint8_t  out[260];
    uint32_t c;
    uint32_t addr;
    uint32_t want;

    for (c = 0u; c < sizeof caps / sizeof caps[0]; c++) {
        for (addr = 0u; addr < 8u; addr++) {
            for (want = 1u; want <= 255u; want++) {
                memset(out, 0xA5, sizeof out);
                read_through(addr + 16u, &out[2], want, caps[c]);
                TEST_ASSERT_EQUAL_MEMORY(&flash[addr + 16u], &out[2], want);
                TEST_ASSERT_EQUAL_HEX8(0xA5, out[0]);
                TEST_ASSERT_EQUAL_HEX8(0xA5, out[1]);
                TEST_ASSERT_EQUAL_HEX8(0xA5, out[2u + want]);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_suit_length_and_envelope_reads_are_aligned);
    RUN_TEST(test_an_aligned_read_is_its_own_window);
    RUN_TEST(test_a_read_past_the_buffer_is_chunked);
    RUN_TEST(test_every_offset_and_length_reads_its_bytes);
    return UNITY_END();
}
