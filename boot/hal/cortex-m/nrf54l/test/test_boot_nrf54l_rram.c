/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's nRF54L15 RRAM write (boot/hal/cortex-m/nrf54l/boot_rram.c, over
 * src/hal/cortex-m/nrf54l/rram_write.h) over the nRF54L register model
 * (test/model/nrf54l_model.h). The RRAM stores go to the test's hooks, named
 * before boot_rram.c is included: each records the RRAMC CONFIG it was made
 * under and its address, value and width, and stores nothing, so no address
 * of the DK's needs mapping.
 */
#include <stdint.h>

#include "nrf54l_model.h"
#include "nrf54l/soc_regs.h"

static unsigned stores, stores_with_wen;

static void store(void)
{
    stores++;
    if (TFW_NRF_RRAMC_CONFIG == TFW_NRF_RRAMC_CONFIG_WEN) {
        stores_with_wen++;
    }
}

/* Each store's address, value and width, in the order made. */
static struct {
    uint32_t addr, value;
    uint8_t width;
} log_[8];
static unsigned logged;

static void record(uint32_t addr, uint32_t value, uint8_t width)
{
    if (logged < sizeof log_ / sizeof log_[0]) {
        log_[logged].addr  = addr;
        log_[logged].value = value;
        log_[logged].width = width;
    }
    logged++;
}

#define TFW_NRF54L_RRAM_STORE32(addr, word) (record((uint32_t)(addr), (uint32_t)(word), 4u), store())
#define TFW_NRF54L_RRAM_STORE8(addr, byte)  (record((uint32_t)(addr), (uint32_t)(byte), 1u), store())

#include "../boot_rram.c"

#include "unity.h"

static const uint8_t data[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };

void setUp(void)
{
    tfw_nrf54l_model_reset();
    stores = stores_with_wen = 0u;
    logged = 0u;
}

void tearDown(void) {}

static void controller_ready(void)
{
    TFW_NRF_RRAMC_READY     = TFW_NRF_RRAMC_READY_BIT;
    TFW_NRF_RRAMC_READYNEXT = TFW_NRF_RRAMC_READY_BIT;
}

/* Every store is made with WEN set... */
static void test_every_store_is_under_wen(void)
{
    controller_ready();
    (void)tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE, data, sizeof data);
    TEST_ASSERT_TRUE(stores != 0u && stores_with_wen == stores);
}

/* ...and WEN is off again once the write returns. */
static void test_wen_is_off_after_the_write(void)
{
    controller_ready();
    (void)tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE, data, sizeof data);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_RRAMC_CONFIG);
}

/* A span that starts below tfw-boot's region end is refused, nothing stored,
 * even where it ends above it. */
static void test_a_span_below_the_region_end_is_refused(void)
{
    controller_ready();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_cm_boot_flash_write(BOOT_REGION_END - 4u, data, sizeof data));
}

/* The refused span stores nothing. */
static void test_a_refused_span_stores_nothing(void)
{
    controller_ready();
    (void)tfw_cm_boot_flash_write(BOOT_REGION_END - 4u, data, sizeof data);
    TEST_ASSERT_EQUAL_UINT(0u, stores);
}

/* A controller that never sets READYNEXT fails the write after the bounded
 * wait rather than holding the core. */
static void test_a_wait_that_never_ends_fails_the_write(void)
{
    TFW_NRF_RRAMC_READY = TFW_NRF_RRAMC_READY_BIT;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT,
                          tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT(0u, logged);
}

/* A controller that is never READY when the write starts fails it with
 * nothing stored and CONFIG left as it was. */
static void test_a_controller_never_ready_at_the_start_stores_nothing(void)
{
    TFW_NRF_RRAMC_READYNEXT = TFW_NRF_RRAMC_READY_BIT;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT,
                          tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT(0u, logged);
    TEST_ASSERT_EQUAL_UINT(0u, stores);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_RRAMC_CONFIG);
}

/* CONFIG gets back the value it had before the write, not zero, both when
 * the write is taken and when a wait times out. */
static void test_config_gets_back_its_earlier_value(void)
{
    TFW_NRF_RRAMC_CONFIG = 0x00000100u;    /* WRITEBUFSIZE 1, WEN clear */
    controller_ready();
    TEST_ASSERT_EQUAL_INT(TFW_OK,
                          tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE, data, sizeof data));
    TEST_ASSERT_TRUE(stores != 0u && stores_with_wen == stores);
    TEST_ASSERT_EQUAL_HEX32(0x00000100u, TFW_NRF_RRAMC_CONFIG);

    TFW_NRF_RRAMC_READYNEXT = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT,
                          tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE, data, sizeof data));
    TEST_ASSERT_EQUAL_HEX32(0x00000100u, TFW_NRF_RRAMC_CONFIG);
}

/* A span that runs past the RRAM's end, or starts beyond it, is refused with
 * nothing stored; the last word is still taken. */
static void test_a_span_past_the_rram_end_is_refused(void)
{
    controller_ready();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_cm_boot_flash_write((uint32_t)TFW_NRF54L_RRAM_SIZE - 2u, data, 4u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_cm_boot_flash_write((uint32_t)TFW_NRF54L_RRAM_SIZE + 4u, data, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, logged);

    TEST_ASSERT_EQUAL_INT(TFW_OK,
                          tfw_cm_boot_flash_write((uint32_t)TFW_NRF54L_RRAM_SIZE - 4u, data, 4u));
    TEST_ASSERT_EQUAL_UINT(1u, logged);
}

/* ...with WEN put back on that path too. */
static void test_a_failed_wait_puts_wen_back(void)
{
    TFW_NRF_RRAMC_READY = TFW_NRF_RRAMC_READY_BIT;
    (void)tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE, data, sizeof data);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_RRAMC_CONFIG);
}

static void assert_store(unsigned i, uint32_t addr, uint32_t value, uint8_t width)
{
    TEST_ASSERT_EQUAL_HEX32(addr, log_[i].addr);
    TEST_ASSERT_EQUAL_HEX32(value, log_[i].value);
    TEST_ASSERT_EQUAL_UINT8(width, log_[i].width);
}

/* An unaligned start is bytes up to the word boundary, each at its own
 * address, then aligned words. */
static void test_each_byte_lands_at_its_address(void)
{
    const uint32_t base = (uint32_t)TFW_CM_PLATFORM_BASE;

    controller_ready();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_cm_boot_flash_write(base + 1u, data, 7u));
    TEST_ASSERT_EQUAL_UINT(4u, logged);
    assert_store(0u, base + 1u, 0x01u, 1u);
    assert_store(1u, base + 2u, 0x02u, 1u);
    assert_store(2u, base + 3u, 0x03u, 1u);
    assert_store(3u, base + 4u, 0x07060504u, 4u);
}

/* A source that is not word-aligned is still stored as one whole word. */
static void test_an_unaligned_source_is_stored_whole(void)
{
    const uint32_t base = (uint32_t)TFW_CM_PLATFORM_BASE;

    controller_ready();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_cm_boot_flash_write(base, data + 1, 4u));
    TEST_ASSERT_EQUAL_UINT(1u, logged);
    assert_store(0u, base, 0x05040302u, 4u);
}

/* A write that starts exactly at tfw-boot's region end is taken; one byte
 * below it is refused with nothing stored. */
static void test_a_write_at_the_region_end_is_accepted(void)
{
    controller_ready();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_cm_boot_flash_write(BOOT_REGION_END, data, 4u));
    TEST_ASSERT_EQUAL_UINT(1u, logged);
    assert_store(0u, BOOT_REGION_END, 0x04030201u, 4u);

    logged = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_cm_boot_flash_write(BOOT_REGION_END - 1u, data, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, logged);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_store_is_under_wen);
    RUN_TEST(test_wen_is_off_after_the_write);
    RUN_TEST(test_a_span_below_the_region_end_is_refused);
    RUN_TEST(test_a_refused_span_stores_nothing);
    RUN_TEST(test_a_wait_that_never_ends_fails_the_write);
    RUN_TEST(test_a_failed_wait_puts_wen_back);
    RUN_TEST(test_a_controller_never_ready_at_the_start_stores_nothing);
    RUN_TEST(test_config_gets_back_its_earlier_value);
    RUN_TEST(test_a_span_past_the_rram_end_is_refused);
    RUN_TEST(test_each_byte_lands_at_its_address);
    RUN_TEST(test_an_unaligned_source_is_stored_whole);
    RUN_TEST(test_a_write_at_the_region_end_is_accepted);
    return UNITY_END();
}
