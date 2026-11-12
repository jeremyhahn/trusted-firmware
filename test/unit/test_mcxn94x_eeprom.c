/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/cortex-m/mcxn94x/hal_eeprom.c over a model of the MCXN947's flash
 * and the boot ROM's driver: an erase sets a sector to 0xFF, a program only
 * clears bits, and the model can be told to program one byte wrong. The
 * driver file is compiled into this one with the model in place of the
 * device's flash, its platform window and the ROM's API tree, and with
 * tfw-boot's own flash helper (boot/hal/cortex-m/mcxn94x/mcxn_flash.c)
 * linked beside it as the firmware links it.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "../../boot/hal/cortex-m/mcxn94x/mcxn_flash.h"

#define MODEL_SIZE   ((uint32_t)TFW_FLASH_TOTAL - TFW_MCXN_FLASH_BASE)
#define OFF(a)       ((uint32_t)(a) - TFW_MCXN_FLASH_BASE)
#define USER_OFF     OFF(TFW_CM_USER_EE_BASE)
#define PLATFORM_OFF OFF(TFW_CM_PLATFORM_BASE)
#define USER_LO      ((uint16_t)(TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN))

static uint8_t  model[MODEL_SIZE];
static unsigned erases;
static unsigned platform_erases;
static unsigned other_erases;
static long     corrupt_at;   /* the model's offset programmed wrong, or -1 */

static int32_t model_init(tfw_mcxn_flash_config_t *config)
{
    config->pflash_block_base  = 0u;
    config->pflash_total_size  = MODEL_SIZE;
    config->pflash_sector_size = TFW_MCXN_FLASH_SECTOR;
    return 0;
}

static int32_t model_erase(tfw_mcxn_flash_config_t *config, uint32_t start,
                           uint32_t length, uint32_t key)
{
    (void)config;
    if (key != TFW_MCXN_ROM_ERASE_KEY || start + length > MODEL_SIZE) {
        return 1;
    }
    erases++;
    if (start < PLATFORM_OFF + TFW_CM_PLATFORM_SIZE && start + length > PLATFORM_OFF) {
        platform_erases++;
    }
    if (start != USER_OFF || length != TFW_CM_USER_EE_SIZE) {
        other_erases++;
    }
    memset(&model[start], 0xFF, length);
    return 0;
}

static int32_t model_program(tfw_mcxn_flash_config_t *config, uint32_t start,
                             uint8_t *src, uint32_t length)
{
    (void)config;
    if (start + length > MODEL_SIZE) {
        return 1;
    }
    for (uint32_t i = 0; i < length; i++) {
        uint8_t v = src[i];

        if (corrupt_at >= 0 && start + i == (uint32_t)corrupt_at) {
            v = (uint8_t)(v & 0xFEu);   /* the program-verify failure */
        }
        model[start + i] &= v;          /* a program only clears bits */
    }
    return 0;
}

static const tfw_mcxn_flash_api_t model_api = {
    .version       = 0u,
    .flash_init    = model_init,
    .flash_erase   = model_erase,
    .flash_program = model_program,
};

static unsigned locks;
static unsigned unlocks;

static uint32_t ee_lock(void)
{
    locks++;
    return 0x5Au;
}

static void ee_unlock(uint32_t primask)
{
    TEST_ASSERT_EQUAL_HEX32(0x5Au, primask);
    unlocks++;
}

#define TFW_MCXN_EE_FLASH  ((const uint8_t *)model)
#define TFW_MCXN_EE_WINDOW (&model[PLATFORM_OFF])
#define TFW_MCXN_EE_API    (&model_api)
#include "../../src/hal/cortex-m/mcxn94x/hal_eeprom.c"

void setUp(void)
{
    memset(model, 0xFF, sizeof model);
    erases = platform_erases = other_erases = 0u;
    locks = unlocks = 0u;
    corrupt_at = -1;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, platform_erases);
    TEST_ASSERT_EQUAL_UINT(0u, other_erases);
    TEST_ASSERT_EQUAL_UINT(locks, unlocks);
}

static void test_the_user_span_fits_the_sector_below_the_platform_block(void)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_PLATFORM_BASE, TFW_CM_USER_EE_BASE + TFW_CM_USER_EE_SIZE);
    TEST_ASSERT_EQUAL_HEX32(TFW_MCXN_FLASH_SECTOR, TFW_CM_USER_EE_SIZE);
    TEST_ASSERT_TRUE(TFW_EEPROM_SIZE - USER_LO <= TFW_CM_USER_EE_SIZE);
}

static void test_a_round_trip_past_the_span_lands_at_the_user_sectors_zero(void)
{
    static const uint8_t data[] = { 0x11, 0x22, 0x33, 0x44, 0x55 };
    uint8_t back[sizeof data];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(USER_LO, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT(1u, erases);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, &model[USER_OFF], sizeof data);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read(USER_LO, back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, back, sizeof data);
}

static void test_a_second_write_keeps_the_rest_of_the_sector(void)
{
    static const uint8_t a[] = { 0xA1, 0xA2 };
    static const uint8_t b[] = { 0xB1, 0xB2, 0xB3 };
    uint8_t back[2];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(USER_LO + 4u, a, sizeof a));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(USER_LO + 100u, b, sizeof b));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read(USER_LO + 4u, back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, back, sizeof a);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, &model[USER_OFF + 100u], sizeof b);
    TEST_ASSERT_EQUAL_HEX8(0xFF, model[USER_OFF + 6u]);
}

static void test_the_same_bytes_again_erase_nothing(void)
{
    static const uint8_t data[] = { 0x01, 0x02 };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(USER_LO, data, sizeof data));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(USER_LO, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT(1u, erases);
}

static void test_the_user_spans_last_byte_lands_and_one_past_it_is_range(void)
{
    const uint8_t v = 0x3C;
    const uint8_t two[2] = { 0x01, 0x02 };
    uint8_t back = 0;
    uint8_t back2[2];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write((uint16_t)(TFW_EEPROM_SIZE - 1u), &v, 1));
    TEST_ASSERT_EQUAL_HEX8(v, model[USER_OFF + (TFW_EEPROM_SIZE - 1u - USER_LO)]);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read((uint16_t)(TFW_EEPROM_SIZE - 1u), &back, 1));
    TEST_ASSERT_EQUAL_HEX8(v, back);

    erases = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)(TFW_EEPROM_SIZE - 1u), two, 2));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)TFW_EEPROM_SIZE, &v, 1));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_read((uint16_t)TFW_EEPROM_SIZE, &back, 1));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_read((uint16_t)(TFW_EEPROM_SIZE - 1u), back2, 2));
    TEST_ASSERT_EQUAL_UINT(0u, erases);
}

static void test_a_write_into_the_bootloaders_span_is_range_and_erases_nothing(void)
{
    const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(0x10u, data, sizeof data));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)(USER_LO - 1u), data, 2));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, data, 1));
    TEST_ASSERT_EQUAL_UINT(0u, erases);
    TEST_ASSERT_EQUAL_HEX8(0xFF, model[PLATFORM_OFF + 0x10u]);
}

static void test_the_confirm_request_keeps_the_store_path_and_erases_nothing(void)
{
    uint8_t req[TFW_BOOT_CONFIRM_LEN];

    memset(req, 0x00, sizeof req);
    req[0] = 0x42;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, req, sizeof req));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(req, &model[PLATFORM_OFF + TFW_BOOT_CONFIRM_ADDR], sizeof req);
    TEST_ASSERT_EQUAL_UINT(0u, erases);
}

static void test_a_read_across_the_spans_end_joins_the_window_and_the_sector(void)
{
    uint8_t back[4];

    model[PLATFORM_OFF + USER_LO - 2u] = 0x01;
    model[PLATFORM_OFF + USER_LO - 1u] = 0x02;
    model[PLATFORM_OFF + USER_LO]      = 0xEE;   /* the window past the span: not read */
    model[USER_OFF]                    = 0x03;
    model[USER_OFF + 1u]               = 0x04;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read((uint16_t)(USER_LO - 2u), back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8(0x01, back[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, back[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, back[2]);
    TEST_ASSERT_EQUAL_HEX8(0x04, back[3]);
}

static void test_a_read_back_mismatch_is_an_error(void)
{
    static const uint8_t data[] = { 0x5B, 0x5B, 0x5B };

    corrupt_at = (long)(USER_OFF + 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_eeprom_write(USER_LO, data, sizeof data));
    TEST_ASSERT_EQUAL_HEX8(0x5A, model[USER_OFF + 1u]);
}

static void test_every_user_byte_written_erases_only_the_user_sector(void)
{
    uint8_t chunk[255];
    uint8_t back[255];
    uint32_t a;

    for (a = USER_LO; a < TFW_EEPROM_SIZE; a += sizeof chunk) {
        uint32_t n = TFW_EEPROM_SIZE - a < sizeof chunk ? TFW_EEPROM_SIZE - a : sizeof chunk;

        for (uint32_t i = 0; i < n; i++) {
            chunk[i] = (uint8_t)(a + i);
        }
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write((uint16_t)a, chunk, (uint8_t)n));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read((uint16_t)a, back, (uint8_t)n));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(chunk, back, n);
    }
    TEST_ASSERT_TRUE(erases > 0u);
    for (a = 0; a < TFW_CM_PLATFORM_SIZE; a++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, model[PLATFORM_OFF + a]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_user_span_fits_the_sector_below_the_platform_block);
    RUN_TEST(test_a_round_trip_past_the_span_lands_at_the_user_sectors_zero);
    RUN_TEST(test_a_second_write_keeps_the_rest_of_the_sector);
    RUN_TEST(test_the_same_bytes_again_erase_nothing);
    RUN_TEST(test_the_user_spans_last_byte_lands_and_one_past_it_is_range);
    RUN_TEST(test_a_write_into_the_bootloaders_span_is_range_and_erases_nothing);
    RUN_TEST(test_the_confirm_request_keeps_the_store_path_and_erases_nothing);
    RUN_TEST(test_a_read_across_the_spans_end_joins_the_window_and_the_sector);
    RUN_TEST(test_a_read_back_mismatch_is_an_error);
    RUN_TEST(test_every_user_byte_written_erases_only_the_user_sector);
    return UNITY_END();
}
