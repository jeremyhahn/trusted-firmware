/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's nRF54L15 RRAM write (boot/hal/cortex-m/nrf54l/boot_rram.c, over
 * src/hal/cortex-m/nrf54l/rram_write.h) over the nRF54L register model
 * (test/model/nrf54l_model.h). The RRAM stores go to the test's hooks, named
 * before boot_rram.c is included: each records the RRAMC CONFIG it was made
 * under and stores nothing, so no address of the DK's needs mapping.
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

#define TFW_NRF54L_RRAM_STORE32(addr, word) ((void)(addr), (void)(word), store())
#define TFW_NRF54L_RRAM_STORE8(addr, byte)  ((void)(addr), (void)(byte), store())

#include "../boot_rram.c"

#include "unity.h"

static const uint8_t data[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };

void setUp(void)
{
    tfw_nrf54l_model_reset();
    stores = stores_with_wen = 0u;
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
}

/* ...with WEN put back on that path too. */
static void test_a_failed_wait_puts_wen_back(void)
{
    TFW_NRF_RRAMC_READY = TFW_NRF_RRAMC_READY_BIT;
    (void)tfw_cm_boot_flash_write((uint32_t)TFW_CM_PLATFORM_BASE, data, sizeof data);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_RRAMC_CONFIG);
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
    return UNITY_END();
}
