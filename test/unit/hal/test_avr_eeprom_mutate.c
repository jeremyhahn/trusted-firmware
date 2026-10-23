/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_eeprom.c alone (#163), over test/mock/avr_regs's
 * <avr/eeprom.h>, <avr/wdt.h> and <avr/pgmspace.h>, which this suite
 * implements over a model of the part's EEPROM cells and flash. Built twice:
 * FLASHEND a 32 KB part's (the near copy) and a 256 KB part's (the far copy).
 *
 * The model counts every program cycle and every watchdog kick, and records a
 * use of eeprom_write_* -- the unit must update, never write, so an unchanged
 * cell costs no wear -- and any address outside the part.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"

#define EE_SIZE    4096u
#define FLASH_SIZE 0x40000ul
#define GUARD      16u

static uint8_t  ee[EE_SIZE];
static uint8_t  flash[FLASH_SIZE];
static unsigned programs, updates, kicks, violations;
static unsigned near_copies, far_copies;
static uint32_t last_far;

static uint8_t ee_at(uintptr_t a)
{
    if (a >= EE_SIZE) {
        violations++;
        return 0xFFu;
    }
    return ee[a];
}

uint8_t eeprom_read_byte(const uint8_t *p)
{
    return ee_at((uintptr_t)p);
}

void eeprom_read_block(void *dst, const void *src, size_t n)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        ((uint8_t *)dst)[i] = ee_at((uintptr_t)src + i);
    }
}

void eeprom_write_byte(uint8_t *p, uint8_t value)
{
    (void)p;
    (void)value;
    violations++;
}

void eeprom_write_block(const void *src, void *dst, size_t n)
{
    (void)src;
    (void)dst;
    (void)n;
    violations++;
}

void eeprom_update_block(const void *src, void *dst, size_t n)
{
    (void)src;
    (void)dst;
    (void)n;
    violations++;
}

void eeprom_update_byte(uint8_t *p, uint8_t value)
{
    uintptr_t a = (uintptr_t)p;

    updates++;
    if (a >= EE_SIZE) {
        violations++;
        return;
    }
    if (ee[a] != value) {
        ee[a] = value;
        programs++;
    }
}

void wdt_reset(void)
{
    kicks++;
}

void *memcpy_P(void *dst, const void *src, size_t n)
{
    uintptr_t a = (uintptr_t)src;

    near_copies++;
    if (a + n > 0x10000u) {
        violations++;
        return dst;
    }
    return memcpy(dst, &flash[a], n);
}

/* uint_farptr_t is uint32_t in avr-libc and in the stand-in. */
void *memcpy_PF(void *dst, uint32_t src, size_t n)
{
    far_copies++;
    last_far = src;
    if ((unsigned long)src + n > FLASH_SIZE) {
        violations++;
        return dst;
    }
    return memcpy(dst, &flash[src], n);
}

static uint8_t pattern(uint32_t a)
{
    return (uint8_t)((a * 7u) ^ (a >> 8) ^ (a >> 16) ^ 0x5Au);
}

void setUp(void)
{
    uint32_t a;

    for (a = 0u; a < EE_SIZE; a++) {
        ee[a] = pattern(a);
    }
    for (a = 0u; a < FLASH_SIZE; a++) {
        flash[a] = pattern(a * 3u + 1u);
    }
    programs = updates = kicks = violations = 0u;
    near_copies = far_copies = 0u;
    last_far = 0u;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, violations);
}

/* A read returns exactly len cells from addr, touches nothing past them, and
 * neither programs a cell nor kicks the watchdog. */
static void check_read(tfw_status_t (*rd)(uint16_t, uint8_t *, uint8_t), uint16_t addr, uint8_t len)
{
    uint8_t  buf[256u + GUARD];
    unsigned i;

    memset(buf, 0xEE, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_OK, rd(addr, buf, len));
    for (i = 0u; i < len; i++) {
        TEST_ASSERT_EQUAL_HEX8(pattern(addr + i), buf[i]);
    }
    for (; i < sizeof buf; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[i]);
    }
    TEST_ASSERT_EQUAL_UINT(0u, updates);
    TEST_ASSERT_EQUAL_UINT(0u, programs);
}

static void test_read_returns_exactly_the_cells_asked(void)
{
    static const uint16_t addrs[] = { 0u, 1u, 0x0100u, 0x03FFu, (uint16_t)(EE_SIZE - 255u) };
    static const uint8_t  lens[]  = { 0u, 1u, 2u, 31u, 255u };
    unsigned a, l;

    for (a = 0u; a < sizeof addrs / sizeof addrs[0]; a++) {
        for (l = 0u; l < sizeof lens; l++) {
            check_read(tfw_hal_eeprom_read, addrs[a], lens[l]);
        }
    }
    check_read(tfw_hal_eeprom_read, (uint16_t)(EE_SIZE - 1u), 1u);
}

/* The platform block's read is the same body at the same address. */
static void test_nvm_read_is_the_eeprom_read(void)
{
    check_read(tfw_hal_nvm_read, 0u, 64u);
    check_read(tfw_hal_nvm_read, 0x0123u, 255u);
    check_read(tfw_hal_nvm_read, 7u, 0u);
}

static void test_write_updates_each_cell_once_and_kicks_per_byte(void)
{
    uint8_t  buf[255];
    unsigned i;

    for (i = 0u; i < sizeof buf; i++) {
        buf[i] = (uint8_t)(i ^ 0xA5u);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(0x0200u, buf, 255u));
    TEST_ASSERT_EQUAL_UINT(255u, updates);
    TEST_ASSERT_EQUAL_UINT(255u, kicks);
    for (i = 0u; i < sizeof buf; i++) {
        TEST_ASSERT_EQUAL_HEX8(buf[i], ee[0x0200u + i]);
    }
    /* The neighbours on both sides are untouched. */
    TEST_ASSERT_EQUAL_HEX8(pattern(0x01FFu), ee[0x01FFu]);
    TEST_ASSERT_EQUAL_HEX8(pattern(0x0200u + 255u), ee[0x0200u + 255u]);
}

static void test_write_of_a_short_run_at_the_edges(void)
{
    const uint8_t two[2] = { 0x11u, 0x22u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(0u, two, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x11u, ee[0]);
    TEST_ASSERT_EQUAL_HEX8(pattern(1u), ee[1]);
    TEST_ASSERT_EQUAL_UINT(1u, updates);
    TEST_ASSERT_EQUAL_UINT(1u, kicks);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write((uint16_t)(EE_SIZE - 2u), two, 2u));
    TEST_ASSERT_EQUAL_HEX8(0x11u, ee[EE_SIZE - 2u]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, ee[EE_SIZE - 1u]);
    TEST_ASSERT_EQUAL_HEX8(pattern(EE_SIZE - 3u), ee[EE_SIZE - 3u]);
    TEST_ASSERT_EQUAL_UINT(3u, updates);
    TEST_ASSERT_EQUAL_UINT(3u, kicks);
}

static void test_write_of_nothing_touches_nothing(void)
{
    const uint8_t one = 0x00u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(0x0010u, &one, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, updates);
    TEST_ASSERT_EQUAL_UINT(0u, kicks);
    TEST_ASSERT_EQUAL_HEX8(pattern(0x0010u), ee[0x0010u]);
}

/* Rewriting what is already there programs no cell: update, never write. */
static void test_unchanged_cells_cost_no_wear(void)
{
    uint8_t  buf[64];
    unsigned i;

    for (i = 0u; i < sizeof buf; i++) {
        buf[i] = pattern(0x0300u + i);
    }
    buf[10] ^= 0xFFu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(0x0300u, buf, (uint8_t)sizeof buf));
    TEST_ASSERT_EQUAL_UINT(sizeof buf, updates);
    TEST_ASSERT_EQUAL_UINT(1u, programs);
    TEST_ASSERT_EQUAL_UINT(sizeof buf, kicks);
}

static void test_written_cells_read_back(void)
{
    uint8_t in[40], out[40];
    unsigned i;

    for (i = 0u; i < sizeof in; i++) {
        in[i] = (uint8_t)(0xC3u + i * 5u);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(0x0777u, in, (uint8_t)sizeof in));
    memset(out, 0, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read(0x0777u, out, (uint8_t)sizeof out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, sizeof in);
}

static void check_flash(uint32_t addr, uint8_t len)
{
    uint8_t  buf[256u + GUARD];
    unsigned i;

    memset(buf, 0xEE, sizeof buf);
    tfw_hal_flash_read(addr, buf, len);
    for (i = 0u; i < len; i++) {
        TEST_ASSERT_EQUAL_HEX8(flash[addr + i], buf[i]);
    }
    for (; i < sizeof buf; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[i]);
    }
}

/* Below 64 KB both parts read the same bytes; the far part through the far
 * copy at the full address, the near part through the near one. */
static void test_flash_read_low(void)
{
    const int far = (unsigned long)FLASHEND > 0xFFFFul;

    check_flash(0u, 16u);
    check_flash(0x1234u, 255u);
    check_flash((uint32_t)FLASHEND, 1u);
    check_flash(0x0042u, 0u);
    TEST_ASSERT_EQUAL_UINT(far ? 0u : 4u, near_copies);
    TEST_ASSERT_EQUAL_UINT(far ? 4u : 0u, far_copies);
}

/* Past 64 KB only the far part has flash; its read keeps every address bit. */
static void test_flash_read_past_64k_keeps_the_high_bits(void)
{
    if ((unsigned long)FLASHEND <= 0xFFFFul) {
        TEST_ASSERT_EQUAL_UINT(0u, far_copies);
        return;
    }
    check_flash(0x10000ul, 8u);
    TEST_ASSERT_EQUAL_HEX32(0x10000ul, last_far);
    check_flash(0x3FF00ul, 255u);
    TEST_ASSERT_EQUAL_HEX32(0x3FF00ul, last_far);
    check_flash(0x1FFF8ul, 16u);
    TEST_ASSERT_EQUAL_HEX32(0x1FFF8ul, last_far);
    TEST_ASSERT_EQUAL_UINT(0u, near_copies);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_read_returns_exactly_the_cells_asked);
    RUN_TEST(test_nvm_read_is_the_eeprom_read);
    RUN_TEST(test_write_updates_each_cell_once_and_kicks_per_byte);
    RUN_TEST(test_write_of_a_short_run_at_the_edges);
    RUN_TEST(test_write_of_nothing_touches_nothing);
    RUN_TEST(test_unchanged_cells_cost_no_wear);
    RUN_TEST(test_written_cells_read_back);
    RUN_TEST(test_flash_read_low);
    RUN_TEST(test_flash_read_past_64k_keeps_the_high_bits);
    return UNITY_END();
}
