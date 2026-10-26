/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's sector erase and program on the RP2350
 * (boot/hal/cortex-m/rp2350/rp_flash_rom.c) run whole over a model of the
 * boot ROM's function table and a 4 MiB flash. The codes are literals from
 * [RP-DS] release 8, c1 | c2 << 8 (§5.4.1): connect_internal_flash 'IF',
 * flash_exit_xip 'EX', flash_range_erase 'RE', flash_range_program 'RP',
 * flash_flush_cache 'FC', flash_enter_cmd_xip 'CX' (§5.4.8.3, .6-.8, .10,
 * .11). The erase is 4 KiB sectors with command 20h, the program 256-byte
 * pages (§5.4.8.9-.11). The model holds each call to the order the ROM
 * requires: nothing but connect while XIP is on, erase and program only with
 * XIP off and interrupts masked, and XIP back on before the write returns.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "cm_regs.h"
#include "rp_flash.h"

#define FLASH_SIZE  0x00400000u  /* the Pico 2 W's 4 MiB */
#define SECTOR      4096u
#define PAGE        256u

#define CODE_IF 0x4649u
#define CODE_EX 0x5845u
#define CODE_RE 0x4552u
#define CODE_RP 0x5052u
#define CODE_FC 0x4346u
#define CODE_CX 0x5843u

static uint8_t flash[FLASH_SIZE];

static char     trace[16];          /* one letter per ROM call */
static uint32_t ntrace;
static uint32_t lookups[8];
static uint32_t nlookups;
static uint32_t missing;            /* the code this ROM lacks, or 0 */
static uint8_t  xip_on;
static uint8_t  connected;
static uint8_t  irq_masked;
static uint32_t irq_saves;
static uint32_t irq_restores;
static uint32_t erase_off, erase_count, erase_block, erase_cmd;
static uint32_t prog_off, prog_count;

uint32_t tfw_cm_irq_save(void)
{
    uint32_t was = irq_masked;

    irq_masked = 1u;
    irq_saves++;
    return was;
}

void tfw_cm_irq_restore(uint32_t primask)
{
    irq_masked = (uint8_t)primask;
    irq_restores++;
}

void tfw_cm_dsb(void) {}
void tfw_cm_isb(void) {}

/* Not called here: the read is through the XIP window. */
volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("rp_flash_rom.c touched a register");
    return NULL;
}

static void call(char c)
{
    TEST_ASSERT_TRUE_MESSAGE(irq_masked, "a ROM flash call with interrupts on");
    TEST_ASSERT_TRUE(ntrace < sizeof trace - 1u);
    trace[ntrace++] = c;
}

static void rom_connect(void)
{
    call('I');
    connected = 1u;
}

static void rom_exit_xip(void)
{
    call('E');
    TEST_ASSERT_TRUE(connected);
    xip_on = 0u;
}

static void rom_erase(uint32_t off, size_t count, uint32_t block_size, uint8_t block_cmd)
{
    call('R');
    TEST_ASSERT_FALSE_MESSAGE(xip_on, "erase with XIP on");
    erase_off = off;
    erase_count = (uint32_t)count;
    erase_block = block_size;
    erase_cmd = block_cmd;
    TEST_ASSERT_EQUAL_UINT32(0u, off % SECTOR);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)count % SECTOR);
    TEST_ASSERT_TRUE(off <= FLASH_SIZE && count <= FLASH_SIZE - off);
    memset(&flash[off], 0xFF, count);
}

/* NOR flash: a program only clears bits, so an unerased page shows. */
static void rom_program(uint32_t off, const uint8_t *data, size_t count)
{
    size_t i;

    call('P');
    TEST_ASSERT_FALSE_MESSAGE(xip_on, "program with XIP on");
    prog_off = off;
    prog_count = (uint32_t)count;
    TEST_ASSERT_EQUAL_UINT32(0u, off % PAGE);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)count % PAGE);
    TEST_ASSERT_TRUE(off <= FLASH_SIZE && count <= FLASH_SIZE - off);
    for (i = 0u; i < count; i++) {
        flash[off + i] &= data[i];
    }
}

static void rom_flush(void)
{
    call('F');
    TEST_ASSERT_FALSE(xip_on);
}

static void rom_enter_xip(void)
{
    call('C');
    xip_on = 1u;
}

uintptr_t tfw_rp_rom_func(uint32_t code)
{
    TEST_ASSERT_TRUE(nlookups < 8u);
    lookups[nlookups++] = code;
    if (code == missing) {
        return 0u;
    }
    switch (code) {
    case CODE_IF: return (uintptr_t)&rom_connect;
    case CODE_EX: return (uintptr_t)&rom_exit_xip;
    case CODE_RE: return (uintptr_t)&rom_erase;
    case CODE_RP: return (uintptr_t)&rom_program;
    case CODE_FC: return (uintptr_t)&rom_flush;
    case CODE_CX: return (uintptr_t)&rom_enter_xip;
    default:      return 0u;
    }
}

static uint8_t buf[SECTOR];

void setUp(void)
{
    uint32_t i;

    memset(flash, 0x00, sizeof flash);   /* written, so an unerased program shows */
    memset(trace, 0, sizeof trace);
    ntrace = 0u;
    nlookups = 0u;
    missing = 0u;
    xip_on = 1u;
    connected = 0u;
    irq_masked = 0u;
    irq_saves = irq_restores = 0u;
    erase_off = erase_count = erase_block = erase_cmd = 0u;
    prog_off = prog_count = 0u;
    for (i = 0u; i < SECTOR; i++) {
        buf[i] = (uint8_t)(i * 7u + 3u);
    }
}

void tearDown(void)
{
    /* Every path, written or refused, leaves XIP on and interrupts as found. */
    TEST_ASSERT_TRUE_MESSAGE(xip_on, "XIP left off");
    TEST_ASSERT_FALSE(irq_masked);
    TEST_ASSERT_EQUAL_UINT32(irq_saves, irq_restores);
}

static void test_the_six_functions_are_looked_up_by_their_codes(void)
{
    static const uint32_t want[6] = { CODE_IF, CODE_EX, CODE_RE, CODE_RP, CODE_FC, CODE_CX };
    uint32_t i;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_sector_write(0u, buf));
    TEST_ASSERT_EQUAL_UINT32(6u, nlookups);
    for (i = 0u; i < 6u; i++) {
        TEST_ASSERT_EQUAL_HEX32(want[i], lookups[i]);
    }
}

static void test_a_sector_is_erased_and_programmed_in_the_roms_order(void)
{
    const uint32_t off = 0x00008000u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_sector_write(off, buf));
    TEST_ASSERT_EQUAL_STRING("IERPFC", trace);
    TEST_ASSERT_EQUAL_HEX32(off, erase_off);
    TEST_ASSERT_EQUAL_UINT32(4096u, erase_count);
    TEST_ASSERT_EQUAL_UINT32(4096u, erase_block);
    TEST_ASSERT_EQUAL_HEX32(0x20u, erase_cmd);
    TEST_ASSERT_EQUAL_HEX32(off, prog_off);
    TEST_ASSERT_EQUAL_UINT32(4096u, prog_count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(buf, &flash[off], SECTOR);
    TEST_ASSERT_EQUAL_HEX8(0x00u, flash[off - 1u]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, flash[off + SECTOR]);
    TEST_ASSERT_EQUAL_UINT32(1u, irq_saves);
}

static void test_the_last_sector_of_the_flash_is_written(void)
{
    const uint32_t off = FLASH_SIZE - SECTOR;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_rp_flash_sector_write(off, buf));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(buf, &flash[off], SECTOR);
}

static void assert_untouched(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, ntrace);
    TEST_ASSERT_EQUAL_UINT32(0u, irq_saves);
}

static void test_an_offset_inside_a_sector_is_refused_untouched(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_rp_flash_sector_write(0x00008100u, buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_rp_flash_sector_write(0x00000001u, buf));
    assert_untouched();
    TEST_ASSERT_EQUAL_UINT32(0u, nlookups);
}

static void test_a_sector_past_the_flash_is_refused_untouched(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_rp_flash_sector_write(FLASH_SIZE, buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_rp_flash_sector_write(0xFFFFF000u, buf));
    assert_untouched();
    TEST_ASSERT_EQUAL_UINT32(0u, nlookups);
}

static void test_a_rom_without_any_one_function_is_refused_untouched(void)
{
    static const uint32_t codes[6] = { CODE_IF, CODE_EX, CODE_RE, CODE_RP, CODE_FC, CODE_CX };
    uint32_t i;

    for (i = 0u; i < 6u; i++) {
        nlookups = 0u;
        missing = codes[i];
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_rp_flash_sector_write(0u, buf));
        assert_untouched();
        TEST_ASSERT_EQUAL_HEX8(0x00u, flash[0]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_six_functions_are_looked_up_by_their_codes);
    RUN_TEST(test_a_sector_is_erased_and_programmed_in_the_roms_order);
    RUN_TEST(test_the_last_sector_of_the_flash_is_written);
    RUN_TEST(test_an_offset_inside_a_sector_is_refused_untouched);
    RUN_TEST(test_a_sector_past_the_flash_is_refused_untouched);
    RUN_TEST(test_a_rom_without_any_one_function_is_refused_untouched);
    return UNITY_END();
}
