/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L15's EEPROM (src/hal/cortex-m/nrf54l/hal_eeprom.c) over a model
 * of its RRAM: the platform window mapped at the DK's own
 * TFW_CM_PLATFORM_BASE, and an RRAM controller whose CONFIG.WEN gates every
 * store, with READY and READYNEXT the test sets (READY can drop after a given
 * store, a controller that never finishes it). A store without WEN programs
 * nothing, as on the part, so a write that lands went through the sequence
 * (rram_write.h, tfw-boot's too).
 *
 * The registers: the host build puts test/model/cortex-m/ ahead of
 * src/hal/cortex-m/, so its cm_regs.h is the one soc_regs.h includes, and
 * TFW_CM_REG(a) is *tfw_imx95_model_reg(a), which this file defines over the
 * three RRAMC words. The stores: rram_write.h's TFW_NRF54L_RRAM_STORE32/8,
 * named here before hal_eeprom.c is included.
 */

#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static void rram_store(uint32_t addr, const void *v, uint32_t n);

#define TFW_NRF54L_RRAM_STORE32(addr, word) \
    do { uint32_t w_ = (word); rram_store((addr), &w_, 4u); } while (0)
#define TFW_NRF54L_RRAM_STORE8(addr, byte) \
    do { uint8_t b_ = (byte); rram_store((addr), &b_, 1u); } while (0)

#include "hal_eeprom.c"

#include "unity.h"
#include "crc16.h"

#define WINDOW ((uint8_t *)(uintptr_t)TFW_CM_PLATFORM_BASE)

static volatile uint32_t config, ready, readynext, other;
static uint32_t          stores, dropped, outside;
static uint32_t          ready_lost_at;    /* READY drops after this store; 0 never */

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    switch (addr) {
    case TFW_NRF_RRAMC_BASE + 0x400u: return &ready;
    case TFW_NRF_RRAMC_BASE + 0x404u: return &readynext;
    case TFW_NRF_RRAMC_BASE + 0x500u: return &config;
    default:                          return &other;
    }
}

static void rram_store(uint32_t addr, const void *v, uint32_t n)
{
    if (addr < TFW_CM_PLATFORM_BASE || addr + n > TFW_CM_PLATFORM_BASE + TFW_CM_PLATFORM_SIZE) {
        outside++;
        return;
    }
    if ((config & TFW_NRF_RRAMC_CONFIG_WEN) == 0u) {
        dropped++;
        return;
    }
    memcpy((uint8_t *)(uintptr_t)addr, v, n);
    stores++;
    if (stores == ready_lost_at) {
        ready = 0u;
    }
}

/* #231's request, as src/core/ota_ctl.c writes it (test_cm_hal_eeprom.c's). */
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

static uint8_t erased(uint32_t addr, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        if (WINDOW[addr + i] != 0xFFu) {
            return 0u;
        }
    }
    return 1u;
}

/* Nothing stored, nothing tried, and CONFIG as the test left it. */
static void assert_untouched(uint32_t cfg)
{
    TEST_ASSERT_EQUAL_UINT32(0u, stores);
    TEST_ASSERT_EQUAL_UINT32(0u, dropped);
    TEST_ASSERT_EQUAL_UINT32(0u, outside);
    TEST_ASSERT_EQUAL_HEX32(cfg, config);
    TEST_ASSERT_TRUE(erased(0u, TFW_CM_PLATFORM_SIZE));
}

void setUp(void)
{
    memset(WINDOW, 0xFF, TFW_CM_PLATFORM_SIZE);
    config    = 0u;
    ready     = TFW_NRF_RRAMC_READY_BIT;
    readynext = TFW_NRF_RRAMC_READY_BIT;
    stores = dropped = outside = 0u;
    ready_lost_at = 0u;
}

void tearDown(void) {}

static void test_the_eeprom_is_the_platform_window(void)
{
    TEST_ASSERT_EQUAL_UINT32(TFW_CM_PLATFORM_SIZE, TFW_EEPROM_SIZE);
    TEST_ASSERT_EQUAL_INT(1, TFW_FEATURE_EEPROM);
}

static void test_a_store_without_wen_programs_nothing(void)
{
    TFW_NRF54L_RRAM_STORE8(TFW_CM_PLATFORM_BASE + TFW_BOOT_EE_LEN, 0x5Au);
    TEST_ASSERT_EQUAL_UINT32(1u, dropped);
    TEST_ASSERT_TRUE(erased(0u, TFW_CM_PLATFORM_SIZE));
}

static void test_a_write_past_the_span_lands_with_wen_and_wen_is_cleared_after(void)
{
    static const uint8_t b[7] = {1u, 2u, 3u, 4u, 5u, 6u, 7u};

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN + 1u, b, sizeof b));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, &WINDOW[TFW_BOOT_EE_LEN + 1u], sizeof b);
    TEST_ASSERT_EQUAL_UINT32(0u, dropped);
    TEST_ASSERT_EQUAL_UINT32(0u, outside);
    TEST_ASSERT_EQUAL_UINT32(4u, stores);    /* 3 bytes to the word, a word, none left */
    TEST_ASSERT_EQUAL_HEX32(0u, config & TFW_NRF_RRAMC_CONFIG_WEN);
    TEST_ASSERT_TRUE(erased(0u, TFW_BOOT_EE_LEN + 1u));
    TEST_ASSERT_TRUE(erased(TFW_BOOT_EE_LEN + 8u, TFW_CM_PLATFORM_SIZE - TFW_BOOT_EE_LEN - 8u));
}

static void test_config_is_put_back_as_it_was(void)
{
    static const uint8_t b[4] = {1u, 2u, 3u, 4u};

    config = 0x00000100u;    /* WRITEBUFSIZE 1, WEN clear */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN, b, sizeof b));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, &WINDOW[TFW_BOOT_EE_LEN], sizeof b);
    TEST_ASSERT_EQUAL_HEX32(0x00000100u, config);
}

static void test_a_controller_that_never_readies_is_timeout_and_wen_is_cleared(void)
{
    static const uint8_t b[4] = {1u, 2u, 3u, 4u};

    readynext = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN, b, sizeof b));
    assert_untouched(0u);
}

static void test_a_controller_not_ready_after_wen_is_timeout_before_any_store(void)
{
    static const uint8_t b[4] = {1u, 2u, 3u, 4u};

    config = 0x00000100u;    /* WRITEBUFSIZE 1, WEN clear */
    ready  = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN, b, sizeof b));
    assert_untouched(0x00000100u);
}

static void test_a_controller_not_ready_after_the_last_store_is_timeout(void)
{
    static const uint8_t b[4] = {1u, 2u, 3u, 4u};

    config        = 0x00000100u;
    ready_lost_at = 1u;      /* the one word's store never completes */
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN, b, sizeof b));
    TEST_ASSERT_EQUAL_UINT32(1u, stores);
    TEST_ASSERT_EQUAL_UINT32(0u, dropped);
    TEST_ASSERT_EQUAL_HEX32(0x00000100u, config);
}

static void test_a_read_is_the_window(void)
{
    uint8_t back[5];

    memcpy(&WINDOW[0x200u], "\x11\x22\x33\x44\x55", sizeof back);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read(0x200u, back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&WINDOW[0x200u], back, sizeof back);
}

static void test_the_windows_last_byte_lands_and_one_past_it_is_range(void)
{
    static const uint8_t b[2] = {0xA5u, 0x5Au};
    uint8_t              back;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)TFW_CM_PLATFORM_SIZE, b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write((uint16_t)(TFW_CM_PLATFORM_SIZE - 1u), b, 2u));
    assert_untouched(0u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write((uint16_t)(TFW_CM_PLATFORM_SIZE - 1u), b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, WINDOW[TFW_CM_PLATFORM_SIZE - 1u]);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read((uint16_t)(TFW_CM_PLATFORM_SIZE - 1u), &back, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, back);
    TEST_ASSERT_EQUAL_UINT32(0u, outside);
}

static void test_a_write_into_the_span_is_refused_before_wen(void)
{
    static const uint8_t b[4] = {1u, 2u, 3u, 4u};
    uint8_t              raw[TFW_BOOT_CONFIRM_LEN];

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, b, sizeof b));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_EE_LEN - 1u, b, 1u));
    confirm_request(raw);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR + 1u, raw, TFW_BOOT_CONFIRM_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, raw, TFW_BOOT_CONFIRM_LEN - 1u));
    assert_untouched(0u);
}

static void test_the_confirm_request_lands_whole_where_tfw_boot_reads_it(void)
{
    uint8_t raw[TFW_BOOT_CONFIRM_LEN], back[TFW_BOOT_CONFIRM_LEN];

    confirm_request(raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, raw, TFW_BOOT_CONFIRM_LEN));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, &WINDOW[TFW_BOOT_CONFIRM_ADDR], TFW_BOOT_CONFIRM_LEN);
    TEST_ASSERT_EQUAL_UINT32(0u, dropped);
    TEST_ASSERT_EQUAL_HEX32(0u, config);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_nvm_read(TFW_BOOT_CONFIRM_ADDR, back, TFW_BOOT_CONFIRM_LEN));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, back, TFW_BOOT_CONFIRM_LEN);
    TEST_ASSERT_TRUE(erased(0u, TFW_BOOT_CONFIRM_ADDR));
    TEST_ASSERT_TRUE(erased(TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN,
                            TFW_CM_PLATFORM_SIZE - TFW_BOOT_CONFIRM_ADDR - TFW_BOOT_CONFIRM_LEN));
}

int main(void)
{
    void *w = mmap(WINDOW, TFW_CM_PLATFORM_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (w != (void *)WINDOW) {
        fprintf(stderr, "test_nrf54l_eeprom: cannot map the platform window at 0x%08lx\n",
                (unsigned long)TFW_CM_PLATFORM_BASE);
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_the_eeprom_is_the_platform_window);
    RUN_TEST(test_a_store_without_wen_programs_nothing);
    RUN_TEST(test_a_write_past_the_span_lands_with_wen_and_wen_is_cleared_after);
    RUN_TEST(test_config_is_put_back_as_it_was);
    RUN_TEST(test_a_controller_that_never_readies_is_timeout_and_wen_is_cleared);
    RUN_TEST(test_a_controller_not_ready_after_wen_is_timeout_before_any_store);
    RUN_TEST(test_a_controller_not_ready_after_the_last_store_is_timeout);
    RUN_TEST(test_a_read_is_the_window);
    RUN_TEST(test_the_windows_last_byte_lands_and_one_past_it_is_range);
    RUN_TEST(test_a_write_into_the_span_is_refused_before_wen);
    RUN_TEST(test_the_confirm_request_lands_whole_where_tfw_boot_reads_it);
    return UNITY_END();
}
