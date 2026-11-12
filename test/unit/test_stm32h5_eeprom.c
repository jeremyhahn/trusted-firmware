/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5's EEPROM (SPEC row stm32h5, src/hal/cortex-m/stm32h5/
 * hal_eeprom.c) on stm32h573i-dk's layout, over tfw-boot's flash driver
 * (boot/hal/cortex-m/stm32h5/h5_flash.c).
 *
 * The flash is mapped at the part's own address, so the platform window
 * hal_nvm.c reads and the user sector hal_eeprom.c reads are where the board
 * profile puts them. The suite is the hardware: a flash model acting at the
 * wait h5_flash.c polls. An erase sets its sector to 0xFF; a program only
 * clears bits (a bit it would set is a programming error, and the quad-word
 * keeps its old value); a program-verify failure leaves the quad-word as it
 * was and reports nothing, as a cell that did not take. The suite also stands
 * in for boot_flash.c's tfw_cm_boot_flash_write, which is the same call on the
 * part's own registers. Every erase is recorded, and no test may erase the
 * platform sector.
 */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "stm32h5/soc_regs.h"
#include "../../boot/hal/cortex-m/boot_soc.h"
#include "../../boot/hal/cortex-m/stm32h5/h5_flash.h"

#define FLASH_BYTES (2u * TFW_H5_FLASH_BANK_SIZE)
#define MEM         ((uint8_t *)(uintptr_t)TFW_H5_FLASH_MEM)
#define REG(off)    (regs[(off) / 4u])
#define USER_OFF    (TFW_CM_USER_EE_BASE - TFW_H5_FLASH_MEM)
#define PLAT_OFF    (TFW_CM_PLATFORM_BASE - TFW_H5_FLASH_MEM)
#define USER_LO     ((uint16_t)TFW_BOOT_EE_LEN)
#define USER_LAST   ((uint16_t)(TFW_EEPROM_SIZE - 1u))

static uint32_t regs[0x100u / 4u];
static uint8_t  shadow[FLASH_BYTES];   /* the flash as the last operation left it */

static struct {
    unsigned erases;
    unsigned platform_erases;
    unsigned violations;
    int      drop;                     /* the next program does not take */
} hw;

static int bits_set(const uint8_t *now, const uint8_t *was, uint32_t n)
{
    uint32_t i;

    for (i = 0u; i < n; i++) {
        if ((uint8_t)(now[i] & (uint8_t)~was[i]) != 0u) {
            return 1;
        }
    }
    return 0;
}

static void do_erase(uint32_t cr)
{
    uint32_t off = ((cr & TFW_H5_FLASH_CR_BKSEL) != 0u ? TFW_H5_FLASH_BANK_SIZE : 0u) +
                   ((cr & TFW_H5_FLASH_CR_SNB) >> TFW_H5_FLASH_CR_SNB_POS) * TFW_H5_FLASH_SECTOR;

    if (memcmp(MEM, shadow, FLASH_BYTES) != 0) {
        hw.violations++;                         /* a store with no PG */
    }
    memset(MEM + off, 0xFF, TFW_H5_FLASH_SECTOR);
    memset(shadow + off, 0xFF, TFW_H5_FLASH_SECTOR);
    hw.erases++;
    if (off == PLAT_OFF) {
        hw.platform_erases++;
    }
}

/* The quad-words the stores changed since the last operation. */
static uint32_t do_program(void)
{
    uint32_t q;

    for (q = 0u; q < FLASH_BYTES; q += TFW_H5_FLASH_QWORD) {
        if (memcmp(MEM + q, shadow + q, TFW_H5_FLASH_QWORD) == 0) {
            continue;
        }
        if (bits_set(MEM + q, shadow + q, TFW_H5_FLASH_QWORD)) {
            memcpy(MEM + q, shadow + q, TFW_H5_FLASH_QWORD);
            hw.violations++;
            return TFW_H5_FLASH_SR_WRPERR << 1;  /* PGSERR */
        }
        if (hw.drop) {
            memcpy(MEM + q, shadow + q, TFW_H5_FLASH_QWORD);
            hw.drop = 0;
            return TFW_H5_FLASH_SR_EOP;
        }
        memcpy(shadow + q, MEM + q, TFW_H5_FLASH_QWORD);
    }
    return TFW_H5_FLASH_SR_EOP;
}

void tfw_h5_flash_wait(const struct tfw_h5_flash *f)
{
    uint32_t cr = REG(TFW_H5_FLASH_NSCR_OFF);

    (void)f;
    REG(TFW_H5_FLASH_NSSR_OFF) &= ~REG(TFW_H5_FLASH_NSCCR_OFF);
    REG(TFW_H5_FLASH_NSCCR_OFF) = 0u;
    if (REG(TFW_H5_FLASH_NSKEYR_OFF) == TFW_H5_FLASH_KEY2) {
        REG(TFW_H5_FLASH_NSCR_OFF) &= ~TFW_H5_FLASH_CR_LOCK;
    }
    REG(TFW_H5_FLASH_NSKEYR_OFF) = 0u;
    if ((cr & (TFW_H5_FLASH_CR_PG | TFW_H5_FLASH_CR_SER)) != 0u
        && (cr & TFW_H5_FLASH_CR_LOCK) != 0u) {
        hw.violations++;
        return;
    }
    if ((cr & (TFW_H5_FLASH_CR_SER | TFW_H5_FLASH_CR_STRT))
        == (TFW_H5_FLASH_CR_SER | TFW_H5_FLASH_CR_STRT)) {
        REG(TFW_H5_FLASH_NSCR_OFF) &= ~TFW_H5_FLASH_CR_STRT;
        do_erase(cr);
        REG(TFW_H5_FLASH_NSSR_OFF) |= TFW_H5_FLASH_SR_EOP;
    } else if ((cr & TFW_H5_FLASH_CR_PG) != 0u) {
        REG(TFW_H5_FLASH_NSSR_OFF) |= do_program();
    }
}

/* boot_flash.c's, on the model's registers. */
tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    static const struct tfw_h5_flash flash = { regs, MEM };

    if (addr < TFW_H5_FLASH_MEM) {
        return TFW_ERR_RANGE;
    }
    return tfw_h5_flash_update(&flash, addr - TFW_H5_FLASH_MEM, data, len);
}

void setUp(void)
{
    uint32_t i;

    memset(MEM, 0xFF, FLASH_BYTES);
    for (i = 0u; i < TFW_CM_PLATFORM_SIZE; i++) {
        MEM[PLAT_OFF + i] = (uint8_t)(i * 7u + 1u);   /* tfw-boot's block */
    }
    memcpy(shadow, MEM, FLASH_BYTES);
    memset(regs, 0, sizeof regs);
    REG(TFW_H5_FLASH_NSCR_OFF) = TFW_H5_FLASH_CR_LOCK;
    memset(&hw, 0, sizeof hw);
}

void tearDown(void)
{
    uint32_t i;

    TEST_ASSERT_EQUAL_UINT(0u, hw.violations);
    TEST_ASSERT_EQUAL_UINT(0u, hw.platform_erases);
    for (i = 0u; i < TFW_CM_PLATFORM_SIZE; i++) {
        if (i < TFW_BOOT_CONFIRM_ADDR || i >= TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN) {
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(i * 7u + 1u), MEM[PLAT_OFF + i]);
        }
    }
}

static void test_the_user_sector_is_the_one_below_the_platform_block(void)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_PLATFORM_BASE - TFW_H5_FLASH_SECTOR, TFW_CM_USER_EE_BASE);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_FLASH_SECTOR, TFW_CM_USER_EE_SIZE);
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_EE_LEN + TFW_CM_USER_EE_SIZE, TFW_EEPROM_SIZE);
}

static void test_a_round_trip_past_the_span(void)
{
    static const uint8_t data[5] = { 0xDEu, 0xADu, 0xBEu, 0xEFu, 0x42u };
    uint8_t back[5];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_write(USER_LO + 3u, data, sizeof data));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, MEM + USER_OFF + 3u, sizeof data);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_read(USER_LO + 3u, back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, back, sizeof back);
    TEST_ASSERT_EQUAL_UINT(0u, hw.erases);      /* erased cells: programmed only */
}

static void test_an_overwrite_rewrites_the_user_sector_and_keeps_its_other_bytes(void)
{
    static const uint8_t first[4]  = { 0x01u, 0x02u, 0x03u, 0x04u };
    static const uint8_t second[4] = { 0xF1u, 0xF2u, 0xF3u, 0xF4u };
    static const uint8_t keep[2]   = { 0x5Au, 0xA5u };
    uint8_t back[4];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_write(USER_LO + 100u, keep, sizeof keep));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_write(USER_LO, first, sizeof first));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_write(USER_LO, second, sizeof second));
    TEST_ASSERT_EQUAL_UINT(1u, hw.erases);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_read(USER_LO, back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(second, back, sizeof back);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(keep, MEM + USER_OFF + 100u, sizeof keep);
}

static void test_the_last_byte_lands_and_one_past_it_is_range(void)
{
    uint8_t b = 0x3Cu, two[2] = { 1u, 2u }, back = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_write(USER_LAST, &b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, MEM[USER_OFF + TFW_CM_USER_EE_SIZE - 1u]);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_read(USER_LAST, &back, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, back);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_eeprom_write(USER_LAST, two, 2u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_eeprom_write(USER_LAST + 1u, &b, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_eeprom_read(USER_LAST, two, 2u));
}

static void test_a_write_into_the_span_is_range_and_touches_no_flash(void)
{
    static const uint8_t data[4] = { 9u, 9u, 9u, 9u };

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_eeprom_write(0u, data, sizeof data));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_eeprom_write(USER_LO - 1u, data, sizeof data));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, data, sizeof data));
    TEST_ASSERT_EQUAL_UINT(0u, hw.erases);
    TEST_ASSERT_EQUAL_MEMORY(shadow, MEM, FLASH_BYTES);
}

static void test_a_read_across_the_spans_end_joins_the_window_and_the_sector(void)
{
    static const uint8_t data[2] = { 0x77u, 0x88u };
    uint8_t back[4];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_write(USER_LO, data, sizeof data));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_read(USER_LO - 2u, back, sizeof back));
    TEST_ASSERT_EQUAL_HEX8(MEM[PLAT_OFF + USER_LO - 2u], back[0]);
    TEST_ASSERT_EQUAL_HEX8(MEM[PLAT_OFF + USER_LO - 1u], back[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, back + 2, sizeof data);
}

static void test_data_that_does_not_read_back_is_internal(void)
{
    static const uint8_t data[4] = { 0x10u, 0x20u, 0x30u, 0x40u };

    hw.drop = 1;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_hal_eeprom_write(USER_LO + 32u, data, sizeof data));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, MEM[USER_OFF + 32u]);
}

static void test_a_rewrite_that_does_not_read_back_is_internal(void)
{
    static const uint8_t first[4]  = { 0x00u, 0x00u, 0x00u, 0x00u };
    static const uint8_t second[4] = { 0xFEu, 0xFEu, 0xFEu, 0xFEu };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_eeprom_write(USER_LO + 64u, first, sizeof first));
    hw.drop = 1;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL,
                          tfw_hal_eeprom_write(USER_LO + 64u, second, sizeof second));
    TEST_ASSERT_EQUAL_UINT(1u, hw.erases);
}

static void test_the_confirm_request_keeps_the_generic_store(void)
{
    uint8_t req[TFW_BOOT_CONFIRM_LEN];

    memset(req, 0xC3, sizeof req);
    /* The store is the generic file's: on the host model it is a plain store
     * into the window, which the flash model counts as no operation. */
    memcpy(shadow + PLAT_OFF + TFW_BOOT_CONFIRM_ADDR, req, sizeof req);
    TEST_ASSERT_EQUAL_INT(TFW_OK,
                          tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, req, sizeof req));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(req, MEM + PLAT_OFF + TFW_BOOT_CONFIRM_ADDR, sizeof req);
    TEST_ASSERT_EQUAL_UINT(0u, hw.erases);
}

int main(void)
{
    void *m = mmap(MEM, FLASH_BYTES, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (m != (void *)MEM) {
        fprintf(stderr, "test_stm32h5_eeprom: cannot map the flash at 0x%08lx\n",
                (unsigned long)TFW_H5_FLASH_MEM);
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_the_user_sector_is_the_one_below_the_platform_block);
    RUN_TEST(test_a_round_trip_past_the_span);
    RUN_TEST(test_an_overwrite_rewrites_the_user_sector_and_keeps_its_other_bytes);
    RUN_TEST(test_the_last_byte_lands_and_one_past_it_is_range);
    RUN_TEST(test_a_write_into_the_span_is_range_and_touches_no_flash);
    RUN_TEST(test_a_read_across_the_spans_end_joins_the_window_and_the_sector);
    RUN_TEST(test_data_that_does_not_read_back_is_internal);
    RUN_TEST(test_a_rewrite_that_does_not_read_back_is_internal);
    RUN_TEST(test_the_confirm_request_keeps_the_generic_store);
    return UNITY_END();
}
