/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Cortex-M EEPROM (src/hal/cortex-m/hal_eeprom.c) on mps2-an505's layout,
 * the soft A/B board: the platform window mapped at the profile's own
 * TFW_CM_PLATFORM_BASE, so the HAL's stores and loads are the ones the image
 * makes.
 *
 * OTA_CONFIRM's request (#231) is 75 bytes at TFW_BOOT_CONFIRM_ADDR, inside
 * [TFW_BOOT_EE_BASE, TFW_BOOT_EE_LEN), which #231 grew to 0x140. The HAL takes
 * that one record whole and refuses every other write below the span's end:
 * the 11-byte request from before #231 among them, so nothing is confirmed.
 * tfw-boot's reading of the record is test/unit/boot/test_boot_swap.c's.
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "crc16.h"

#define WINDOW ((uint8_t *)(uintptr_t)TFW_CM_PLATFORM_BASE)

/* #231's request: [magic 0xC9:1][measurement:8][signature:64][crc:2], the CRC
 * over the 73 bytes before it, as src/core/ota_ctl.c writes it. */
static void confirm_request(uint8_t raw[TFW_BOOT_CONFIRM_LEN])
{
    uint16_t crc;
    unsigned i;

    raw[0] = TFW_BOOT_CONFIRM_MAGIC;
    for (i = 0u; i < TFW_BOOT_CONFIRM_MEAS_LEN; i++) {
        raw[TFW_BOOT_CONFIRM_O_MEAS + i] = (uint8_t)(0xA0u + i);
    }
    for (i = 0u; i < TFW_BOOT_CONFIRM_SIG_LEN; i++) {
        raw[TFW_BOOT_CONFIRM_O_SIG + i] = (uint8_t)(i * 7u + 1u);
    }
    crc = tfw_crc16(raw, TFW_BOOT_CONFIRM_O_CRC);
    raw[TFW_BOOT_CONFIRM_O_CRC]      = (uint8_t)crc;
    raw[TFW_BOOT_CONFIRM_O_CRC + 1u] = (uint8_t)(crc >> 8);
}

/* The request before #231: [magic][measurement:8][crc:2] over the 9 before. */
static void old_request(uint8_t raw[11])
{
    uint16_t crc;
    unsigned i;

    raw[0] = TFW_BOOT_CONFIRM_MAGIC;
    for (i = 0u; i < 8u; i++) {
        raw[1u + i] = (uint8_t)(0xA0u + i);
    }
    crc     = tfw_crc16(raw, 9u);
    raw[9]  = (uint8_t)crc;
    raw[10] = (uint8_t)(crc >> 8);
}

static uint8_t erased(uint16_t addr, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len; i++) {
        if (WINDOW[addr + i] != 0xFFu) {
            return 0u;
        }
    }
    return 1u;
}

void setUp(void)
{
    memset(WINDOW, 0xFF, TFW_CM_PLATFORM_SIZE);
}

void tearDown(void) {}

static void test_the_confirm_request_lies_between_the_block_and_the_spans_end(void)
{
    TEST_ASSERT_EQUAL_UINT(75u, TFW_BOOT_CONFIRM_LEN);
    TEST_ASSERT_EQUAL_HEX16(0x140u, TFW_BOOT_EE_LEN);
    TEST_ASSERT_TRUE(TFW_BOOT_CONFIRM_ADDR >= TFW_BOOT_EE_BASE + TFW_BOOT_EE_BLOCK_LEN);
    TEST_ASSERT_TRUE(TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN <= TFW_BOOT_EE_LEN);
}

static void test_the_75_byte_request_lands_whole_where_tfw_boot_reads_it(void)
{
    uint8_t raw[TFW_BOOT_CONFIRM_LEN], back[TFW_BOOT_CONFIRM_LEN];

    confirm_request(raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, raw, TFW_BOOT_CONFIRM_LEN));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, &WINDOW[TFW_BOOT_CONFIRM_ADDR], TFW_BOOT_CONFIRM_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_nvm_read(TFW_BOOT_CONFIRM_ADDR, back, TFW_BOOT_CONFIRM_LEN));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, back, TFW_BOOT_CONFIRM_LEN);
    TEST_ASSERT_TRUE(erased(0u, TFW_BOOT_CONFIRM_ADDR));
    TEST_ASSERT_TRUE(erased(TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN,
                            (uint16_t)(TFW_CM_PLATFORM_SIZE - TFW_BOOT_CONFIRM_ADDR - TFW_BOOT_CONFIRM_LEN)));
}

static void test_the_11_byte_request_is_range_and_nothing_is_confirmed(void)
{
    uint8_t raw[11];

    old_request(raw);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, raw, sizeof raw));
    TEST_ASSERT_TRUE(erased(0u, (uint16_t)TFW_CM_PLATFORM_SIZE));
}

static void test_the_request_one_byte_off_its_place_is_range(void)
{
    uint8_t raw[TFW_BOOT_CONFIRM_LEN];

    confirm_request(raw);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR - 1u, raw, TFW_BOOT_CONFIRM_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR + 1u, raw, TFW_BOOT_CONFIRM_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, raw, TFW_BOOT_CONFIRM_LEN - 1u));
    TEST_ASSERT_TRUE(erased(0u, (uint16_t)TFW_CM_PLATFORM_SIZE));
}

static void test_a_write_into_the_platform_block_is_range(void)
{
    static const uint8_t b[4] = {1u, 2u, 3u, 4u};

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, b, sizeof b));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_EE_BLOCK_LEN - 1u, b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN - 1u, b, 1u));
    TEST_ASSERT_TRUE(erased(0u, (uint16_t)TFW_CM_PLATFORM_SIZE));
}

static void test_a_write_above_the_span_lands_and_one_past_the_window_is_range(void)
{
    static const uint8_t b[4] = {1u, 2u, 3u, 4u};

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN, b, sizeof b));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, &WINDOW[TFW_BOOT_EE_LEN], sizeof b);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)(TFW_CM_PLATFORM_SIZE - 3u), b, sizeof b));
    TEST_ASSERT_TRUE(erased((uint16_t)(TFW_CM_PLATFORM_SIZE - 3u), 3u));
}

/* The EEPROM family's bounds are the window's (TFW_EEPROM_SIZE, the board
 * profile): the user span [TFW_BOOT_EE_LEN, TFW_EEPROM_SIZE) round-trips up to
 * the window's last byte, one byte past it is range both ways, and a write
 * that reaches back into tfw-boot's span is range and lands nothing. */
static void test_eeprom_user_span_roundtrip(void)
{
    uint8_t in[16];
    uint8_t out[16];
    uint8_t last = 0x5Au;
    uint8_t got  = 0u;
    uint8_t i;

    TEST_ASSERT_EQUAL(1, TFW_FEATURE_EEPROM);
    TEST_ASSERT_EQUAL_UINT32(TFW_CM_PLATFORM_SIZE, TFW_EEPROM_SIZE);
    for (i = 0u; i < sizeof in; i++) {
        in[i] = (uint8_t)(i ^ 0xA5u);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN, in, sizeof in));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read(TFW_BOOT_EE_LEN, out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, sizeof in);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write((uint16_t)(TFW_EEPROM_SIZE - 1u), &last, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read((uint16_t)(TFW_EEPROM_SIZE - 1u), &got, 1u));
    TEST_ASSERT_EQUAL_HEX8(last, got);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)TFW_EEPROM_SIZE, &last, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_read((uint16_t)TFW_EEPROM_SIZE, &got, 1u));

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)(TFW_BOOT_EE_LEN - 1u), in, 2u));
    TEST_ASSERT_TRUE(erased((uint16_t)(TFW_BOOT_EE_LEN - 1u), 1u));
}

int main(void)
{
    void *w = mmap(WINDOW, TFW_CM_PLATFORM_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (w != (void *)WINDOW) {
        fprintf(stderr, "test_cm_hal_eeprom: cannot map the platform window at 0x%08lx\n",
                (unsigned long)TFW_CM_PLATFORM_BASE);
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_the_confirm_request_lies_between_the_block_and_the_spans_end);
    RUN_TEST(test_the_75_byte_request_lands_whole_where_tfw_boot_reads_it);
    RUN_TEST(test_the_11_byte_request_is_range_and_nothing_is_confirmed);
    RUN_TEST(test_the_request_one_byte_off_its_place_is_range);
    RUN_TEST(test_a_write_into_the_platform_block_is_range);
    RUN_TEST(test_a_write_above_the_span_lands_and_one_past_the_window_is_range);
    RUN_TEST(test_eeprom_user_span_roundtrip);
    return UNITY_END();
}
