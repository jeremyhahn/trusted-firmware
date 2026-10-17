/*
 * OTA_INFO on the element tier (#77): the floor it reports is the element's
 * monotonic counter, the one tfw-boot enforces, not the platform block's copy,
 * and an element it cannot read is an error rather than the block's value.
 * The software tier's floor is test_ota_stage.c's.
 */

#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/board.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "atecc608a_model.h"
#include "ota_ctl.h"
#include "boot_platform.h"
#include "tfw/hal.h"

void setUp(void)    { tfw_nor_model_reset(); tfw_model_reset(); }
void tearDown(void) { tfw_model_set_absent(0u); tfw_model_force_status(0u); }

static void put_block(uint32_t floor_in_block)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.rollback_floor = floor_in_block;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_LEN));
}

static tfw_status_t info(uint8_t out[44])
{
    uint8_t      slot = 0u;
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, &slot, 1u);
    tfw_writer_init(&w, out, 44u);
    return tfw_otactl_cmd_info(&r, &w);
}

static uint32_t rd32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* An update advanced the counter past the block's copy: the counter is reported. */
static void test_info_reports_the_counter_past_the_block(void)
{
    uint8_t out[44];

    put_block(3u);
    tfw_model_set_counter(9u);
    TEST_ASSERT_EQUAL(TFW_OK, info(out));
    TEST_ASSERT_EQUAL_UINT32(9u, rd32(&out[3]));
}

/* A failed counter read is OTA_INFO's error, never the block's floor. */
static void test_info_fails_when_the_counter_cannot_be_read(void)
{
    uint8_t out[44];

    put_block(3u);
    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, info(out));
    tfw_model_force_status(0u);
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, info(out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_info_reports_the_counter_past_the_block);
    RUN_TEST(test_info_fails_when_the_counter_cannot_be_read);
    return UNITY_END();
}
