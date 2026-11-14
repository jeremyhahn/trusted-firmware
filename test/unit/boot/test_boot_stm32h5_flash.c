/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's STM32H5 flash driver (boot/hal/cortex-m/stm32h5/h5_flash.c) on a
 * flash interface and a 2 MiB flash of this file's own. The driver's one call
 * out, tfw_h5_flash_wait, is defined here, and that is where this "hardware"
 * acts on what the driver wrote to its registers:
 *
 *   - NSCCR's ones clear the same flags in NSSR;
 *   - FLASH_KEY2, the second key, last in NSKEYR clears LOCK in NSCR;
 *   - SER and STRT erase the sector SNB and BKSEL name;
 *   - PG takes the stores made since the last operation, which must be one
 *     quad-word, erased until then, with LOCK clear.
 *
 * Anything else -- a store with no PG, a quad-word programmed twice between
 * erases, an operation under LOCK -- is counted as a violation, and every test
 * ends with none. The registers' offsets and bits are literals from ST's
 * stm32h573xx.h (FLASH_TypeDef, FLASH_SR_*, FLASH_CR_*) and the keys from
 * stm32h5xx_hal_flash.h, never the driver's own names, so a wrong value in
 * src/hal/cortex-m/stm32h5/soc_regs.h fails here. The offsets into the flash
 * are the board's: the application slot at 0x8000, the platform block's
 * sector at 0xFE000 (boards/stm32h573i-dk/board_profile.h).
 */

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "../../../boot/hal/cortex-m/stm32h5/h5_flash.h"

#define KEYR   (0x04u / 4u)
#define SR     (0x20u / 4u)
#define CR     (0x28u / 4u)
#define CCR    (0x30u / 4u)

#define SR_EOP     (0x1u << 16)
#define SR_WRPERR  (0x1u << 17)
#define SR_PGSERR  (0x1u << 18)
#define CR_LOCK    (0x1u << 0)
#define CR_PG      (0x1u << 1)
#define CR_SER     (0x1u << 2)
#define CR_STRT    (0x1u << 5)
#define CR_BKSEL   (0x1u << 31)
#define KEY2       0xCDEF89ABu

#define FLASH_BYTES  0x200000u
#define BANK_BYTES   0x100000u
#define SECTOR       0x2000u
#define QWORD        16u

static uint32_t regs[0x100u / 4u];
static uint32_t mem_words[FLASH_BYTES / 4u];
static uint8_t  shadow[FLASH_BYTES];   /* the flash as the last operation left it */
#define MEM ((uint8_t *)mem_words)

static const struct tfw_h5_flash flash = { regs, (uint8_t *)mem_words };

static struct {
    int      key_ok;       /* the part takes the key */
    uint32_t fail;         /* NSSR error flags the next operation raises */
    int      drop;         /* the next program does nothing and says nothing */
    unsigned keys, erases, programs, violations;
    uint32_t erased;       /* the last erased sector's offset */
    uint32_t cleared;      /* every flag NSCCR has cleared */
    uint32_t masked;       /* PRIMASK as the hooks hold it */
    unsigned masked_saves; /* quad-words stored under the mask */
    unsigned unmasked_stores; /* stores the mask did not cover */
    int      unmasked;     /* at_unmask holds the flash as unmasked */
} hw;
static uint8_t  at_unmask[FLASH_BYTES]; /* the flash when the mask came off */

static int blank(const uint8_t *p, uint32_t n)
{
    uint32_t i;

    for (i = 0u; i < n; i++) {
        if (p[i] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

/* The one quad-word stored to since the last operation, or -1 if none or more
 * than one. */
static long stored_qword(void)
{
    long found = -1;
    uint32_t s, q;

    for (s = 0u; s < FLASH_BYTES; s += SECTOR) {
        if (memcmp(MEM + s, shadow + s, SECTOR) == 0) {
            continue;
        }
        for (q = s; q < s + SECTOR; q += QWORD) {
            if (memcmp(MEM + q, shadow + q, QWORD) != 0) {
                if (found >= 0) {
                    return -1;
                }
                found = (long)q;
            }
        }
    }
    return found;
}

static void do_erase(uint32_t cr)
{
    uint32_t off = ((cr & CR_BKSEL) != 0u ? BANK_BYTES : 0u) + ((cr >> 6) & 0x7Fu) * SECTOR;

    if (memcmp(MEM, shadow, FLASH_BYTES) != 0) {
        hw.violations++;                      /* a store with no PG */
    }
    memset(MEM + off, 0xFF, SECTOR);
    memset(shadow + off, 0xFF, SECTOR);
    hw.erases++;
    hw.erased = off;
}

static void do_program(void)
{
    long q = stored_qword();

    if (hw.unmasked && memcmp(MEM, at_unmask, FLASH_BYTES) != 0) {
        hw.unmasked_stores++;                 /* stored after the mask came off */
    }
    if (q < 0 || !blank(shadow + q, QWORD)) {
        hw.violations++;                      /* not one erased quad-word */
        return;
    }
    if (hw.fail != 0u || hw.drop) {
        memcpy(MEM + q, shadow + q, QWORD);   /* refused */
        hw.drop = 0;
        return;
    }
    memcpy(shadow + q, MEM + q, QWORD);
    hw.programs++;
}

void tfw_h5_flash_wait(const struct tfw_h5_flash *f)
{
    uint32_t cr = regs[CR];

    (void)f;                                  /* always `flash`, on regs */
    regs[SR] &= ~regs[CCR];
    hw.cleared |= regs[CCR];
    regs[CCR] = 0u;
    if (regs[KEYR] != 0u) {
        hw.keys++;
        if (hw.key_ok && regs[KEYR] == KEY2) {
            regs[CR] &= ~CR_LOCK;
        }
        regs[KEYR] = 0u;
    }
    if ((cr & (CR_PG | CR_SER)) != 0u && (cr & CR_LOCK) != 0u) {
        hw.violations++;
        return;
    }
    if ((cr & (CR_SER | CR_STRT)) == (CR_SER | CR_STRT)) {
        regs[CR] &= ~CR_STRT;
        if (hw.fail == 0u) {
            do_erase(cr);
        }
    } else if ((cr & CR_PG) != 0u) {
        do_program();
    } else {
        return;
    }
    regs[SR] |= hw.fail != 0u ? hw.fail : SR_EOP;
    hw.fail = 0u;
}

/* The stores between save and restore are the pending quad-word: none may be
 * pending when the mask goes on, and the whole of it is stored before the mask
 * comes off (do_program finds no store after the snapshot taken here). */
uint32_t tfw_h5_flash_irq_save(void)
{
    uint32_t was = hw.masked;

    if (memcmp(MEM, shadow, FLASH_BYTES) != 0) {
        hw.unmasked_stores++;                 /* stored before the mask */
    }
    hw.masked = 1u;
    hw.masked_saves++;
    hw.unmasked = 0;
    return was;
}

void tfw_h5_flash_irq_restore(uint32_t mask)
{
    hw.masked = mask;
    if (mask == 0u) {
        if (memcmp(MEM, shadow, FLASH_BYTES) == 0) {
            hw.unmasked_stores++;             /* nothing stored under the mask */
        }
        memcpy(at_unmask, MEM, FLASH_BYTES);
        hw.unmasked = 1;
    }
}

/* Bytes an earlier write left programmed. */
static void seed(uint32_t off, const uint8_t *data, uint32_t len)
{
    memcpy(MEM + off, data, len);
    memcpy(shadow + off, data, len);
}

static void fill(uint8_t *p, uint32_t len, uint8_t start)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        p[i] = (uint8_t)(start + i * 7u);
        if (p[i] == 0xFFu) {
            p[i] = 0x5Au;
        }
    }
}

/* No stray store, no violation, and the interface as a write must leave it:
 * locked, no operation selected. */
static void assert_settled(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, hw.violations);
    TEST_ASSERT_EQUAL_UINT(0u, hw.unmasked_stores);
    TEST_ASSERT_EQUAL_MEMORY(shadow, MEM, FLASH_BYTES);
    TEST_ASSERT_EQUAL_HEX32(CR_LOCK, regs[CR]);
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    regs[CR] = CR_LOCK;                       /* NSCR's reset value */
    memset(mem_words, 0xFF, sizeof mem_words);
    memset(shadow, 0xFF, sizeof shadow);
    memset(&hw, 0, sizeof hw);
    hw.key_ok = 1;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, hw.unmasked_stores);
}

/* --------------------------------------------------------------- no write */

static void test_a_write_that_matches_writes_nothing(void)
{
    uint8_t page[256];

    fill(page, sizeof page, 1u);
    seed(0x8000u, page, sizeof page);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0x8000u, page, sizeof page));
    TEST_ASSERT_EQUAL_UINT(0u, hw.keys);
    TEST_ASSERT_EQUAL_UINT(0u, hw.programs);
    TEST_ASSERT_EQUAL_UINT(0u, hw.erases);
    assert_settled();
}

static void test_a_zero_length_write_writes_nothing(void)
{
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE000u, &b, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, hw.keys);
    assert_settled();
}

/* ----------------------------------------------------------- program only */

static void test_an_erased_page_is_programmed_without_an_erase(void)
{
    uint8_t page[256];

    fill(page, sizeof page, 3u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0x8000u, page, sizeof page));
    TEST_ASSERT_EQUAL_UINT(1u, hw.keys);
    TEST_ASSERT_EQUAL_UINT(16u, hw.programs);
    TEST_ASSERT_EQUAL_UINT(0u, hw.erases);
    TEST_ASSERT_EQUAL_MEMORY(page, MEM + 0x8000u, sizeof page);
    TEST_ASSERT_TRUE(hw.cleared & SR_EOP);
    assert_settled();
}

static void test_erased_quad_words_in_the_data_are_not_programmed(void)
{
    uint8_t page[256];

    fill(page, sizeof page, 5u);
    memset(page, 0xFF, 16u);
    memset(page + 32u, 0xFF, 16u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0x8000u, page, sizeof page));
    TEST_ASSERT_EQUAL_UINT(14u, hw.programs);
    TEST_ASSERT_EQUAL_MEMORY(page, MEM + 0x8000u, sizeof page);
    assert_settled();
}

static void test_a_short_write_programs_the_quad_words_it_touches(void)
{
    static const uint8_t field[4] = { 0x11u, 0x22u, 0x33u, 0x44u };

    /* 0xFE00E to 0xFE011: the last two bytes of one quad-word, the first two
     * of the next. */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE00Eu, field, sizeof field));
    TEST_ASSERT_EQUAL_UINT(2u, hw.programs);
    TEST_ASSERT_EQUAL_UINT(0u, hw.erases);
    TEST_ASSERT_EQUAL_MEMORY(field, MEM + 0xFE00Eu, sizeof field);
    TEST_ASSERT_TRUE(blank(MEM + 0xFE000u, 14u));
    TEST_ASSERT_TRUE(blank(MEM + 0xFE012u, 14u));
    assert_settled();
}

static void test_the_key_is_written_only_to_a_locked_interface(void)
{
    static const uint8_t field[4] = { 1u, 2u, 3u, 4u };

    regs[CR] = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE000u, field, sizeof field));
    TEST_ASSERT_EQUAL_UINT(0u, hw.keys);
    TEST_ASSERT_EQUAL_UINT(1u, hw.programs);
    assert_settled();
}

/* ----------------------------------------------------------------- rewrite */

static void test_a_programmed_page_rewrites_its_sector(void)
{
    static uint8_t old[SECTOR];
    uint8_t page[256];

    fill(old, sizeof old, 9u);
    seed(0x8000u, old, sizeof old);
    fill(page, sizeof page, 77u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0x8100u, page, sizeof page));
    TEST_ASSERT_EQUAL_UINT(1u, hw.erases);
    TEST_ASSERT_EQUAL_HEX32(0x8000u, hw.erased);              /* bank 1, sector 4 */
    TEST_ASSERT_EQUAL_UINT(SECTOR / QWORD, hw.programs);
    TEST_ASSERT_EQUAL_MEMORY(old, MEM + 0x8000u, 0x100u);
    TEST_ASSERT_EQUAL_MEMORY(page, MEM + 0x8100u, sizeof page);
    TEST_ASSERT_EQUAL_MEMORY(old + 0x200u, MEM + 0x8200u, SECTOR - 0x200u);
    assert_settled();
}

static void test_a_field_beside_a_programmed_one_rewrites_the_sector(void)
{
    static const uint8_t first[4]  = { 0xA1u, 0xA2u, 0xA3u, 0xA4u };
    static const uint8_t second[4] = { 0xB1u, 0xB2u, 0xB3u, 0xB4u };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE000u, first, sizeof first));
    TEST_ASSERT_EQUAL_UINT(0u, hw.erases);
    /* The same quad-word: it cannot be programmed twice between erases. */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE004u, second, sizeof second));
    TEST_ASSERT_EQUAL_UINT(2u, hw.keys);
    TEST_ASSERT_EQUAL_UINT(1u, hw.erases);
    TEST_ASSERT_EQUAL_HEX32(0xFE000u, hw.erased);             /* bank 1, sector 127 */
    TEST_ASSERT_EQUAL_UINT(2u, hw.programs);                   /* once each time */
    TEST_ASSERT_EQUAL_MEMORY(first, MEM + 0xFE000u, sizeof first);
    TEST_ASSERT_EQUAL_MEMORY(second, MEM + 0xFE004u, sizeof second);
    TEST_ASSERT_TRUE(blank(MEM + 0xFE008u, SECTOR - 8u));
    assert_settled();
}

/* The rewrite programs the whole sector back: a quad-word restored for another
 * field that does not take fails the update, though the data read back. */
static void test_a_rewrite_that_loses_another_field_is_internal(void)
{
    static const uint8_t kept[4]   = { 0xA1u, 0xA2u, 0xA3u, 0xA4u };
    static const uint8_t first[4]  = { 0x00u, 0x00u, 0x00u, 0x00u };
    static const uint8_t second[4] = { 0xB1u, 0xB2u, 0xB3u, 0xB4u };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE000u, kept, sizeof kept));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE040u, first, sizeof first));
    hw.drop = 1;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL,
                          tfw_h5_flash_update(&flash, 0xFE040u, second, sizeof second));
    TEST_ASSERT_EQUAL_UINT(1u, hw.erases);
    TEST_ASSERT_EQUAL_MEMORY(second, MEM + 0xFE040u, sizeof second);
}

/* Each quad-word's stores run masked and the mask is put back as it was:
 * enabled for the application, still masked for tfw-boot. */
static void test_the_stores_run_masked_and_the_mask_is_put_back(void)
{
    static const uint8_t first[4]  = { 0xA1u, 0xA2u, 0xA3u, 0xA4u };
    static const uint8_t second[4] = { 0xB1u, 0xB2u, 0xB3u, 0xB4u };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE000u, first, sizeof first));
    TEST_ASSERT_EQUAL_UINT(hw.programs, hw.masked_saves);
    TEST_ASSERT_EQUAL_UINT32(0u, hw.masked);
    hw.masked = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE004u, second, sizeof second));
    TEST_ASSERT_EQUAL_UINT(hw.programs, hw.masked_saves);
    TEST_ASSERT_EQUAL_UINT32(1u, hw.masked);
    TEST_ASSERT_EQUAL_UINT(0u, hw.unmasked_stores);
}

/* Codex P1 on #394: the model sees where the stores fall against the mask, so
 * a store before it, a mask lifted before the stores, or a quad-word split by
 * the restore each count. */
static void test_the_model_counts_stores_outside_the_mask(void)
{
    uint32_t m;

    MEM[0x8000u] = 0x00u;
    m = tfw_h5_flash_irq_save();
    TEST_ASSERT_EQUAL_UINT(1u, hw.unmasked_stores);
    tfw_h5_flash_irq_restore(m);
    do_program();

    hw.unmasked_stores = 0u;
    m = tfw_h5_flash_irq_save();
    tfw_h5_flash_irq_restore(m);
    TEST_ASSERT_EQUAL_UINT(1u, hw.unmasked_stores);

    hw.unmasked_stores = 0u;
    m = tfw_h5_flash_irq_save();
    MEM[0x8010u] = 0x00u;
    tfw_h5_flash_irq_restore(m);
    MEM[0x8014u] = 0x00u;
    do_program();
    TEST_ASSERT_EQUAL_UINT(1u, hw.unmasked_stores);
    hw.unmasked_stores = 0u;
}

static void test_a_sector_in_bank_2_is_erased_through_bksel(void)
{
    uint8_t old[QWORD];
    uint8_t now[QWORD];

    fill(old, sizeof old, 20u);
    fill(now, sizeof now, 40u);
    seed(0x104000u, old, sizeof old);                          /* bank 2, sector 2 */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0x104000u, now, sizeof now));
    TEST_ASSERT_EQUAL_UINT(1u, hw.erases);
    TEST_ASSERT_EQUAL_HEX32(0x104000u, hw.erased);
    TEST_ASSERT_EQUAL_MEMORY(now, MEM + 0x104000u, sizeof now);
    assert_settled();
}

static void test_a_write_across_two_sectors_rewrites_both(void)
{
    uint8_t old[32];
    uint8_t now[16];

    fill(old, sizeof old, 50u);
    fill(now, sizeof now, 90u);
    seed(0x9FF0u, old, sizeof old);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0x9FF8u, now, sizeof now));
    TEST_ASSERT_EQUAL_UINT(2u, hw.erases);
    TEST_ASSERT_EQUAL_HEX32(0xA000u, hw.erased);
    TEST_ASSERT_EQUAL_MEMORY(old, MEM + 0x9FF0u, 8u);
    TEST_ASSERT_EQUAL_MEMORY(now, MEM + 0x9FF8u, sizeof now);
    TEST_ASSERT_EQUAL_MEMORY(old + 24u, MEM + 0xA008u, 8u);
    assert_settled();
}

/* ------------------------------------------------------------------ errors */

static void test_a_write_past_bank_2_is_out_of_range(void)
{
    uint8_t buf[32];

    fill(buf, sizeof buf, 1u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_h5_flash_update(&flash, 0x1FFFF0u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_h5_flash_update(&flash, 0xFFFFFFF0u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_h5_flash_update(&flash, 0x200001u, buf, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, hw.keys);
    assert_settled();
}

static void test_an_interface_that_stays_locked_is_a_protected_region(void)
{
    static const uint8_t field[4] = { 1u, 2u, 3u, 4u };

    hw.key_ok = 0;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_h5_flash_update(&flash, 0xFE000u, field, sizeof field));
    TEST_ASSERT_EQUAL_UINT(1u, hw.keys);
    TEST_ASSERT_EQUAL_UINT(0u, hw.programs);
    assert_settled();
}

static void test_a_write_protection_error_is_a_protected_region(void)
{
    static const uint8_t field[4] = { 1u, 2u, 3u, 4u };

    hw.fail = SR_WRPERR;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_h5_flash_update(&flash, 0xFE000u, field, sizeof field));
    TEST_ASSERT_EQUAL_HEX32(SR_WRPERR, regs[CCR]);           /* cleared through NSCCR */
    TEST_ASSERT_TRUE(blank(MEM + 0xFE000u, QWORD));
    assert_settled();
}

static void test_a_programming_error_is_internal(void)
{
    static const uint8_t field[4] = { 1u, 2u, 3u, 4u };

    hw.fail = SR_PGSERR;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL,
                          tfw_h5_flash_update(&flash, 0xFE000u, field, sizeof field));
    TEST_ASSERT_EQUAL_HEX32(SR_PGSERR, regs[CCR]);
    assert_settled();
}

static void test_an_erase_error_stops_the_rewrite(void)
{
    uint8_t old[QWORD];
    uint8_t now[QWORD];

    fill(old, sizeof old, 20u);
    fill(now, sizeof now, 40u);
    seed(0x8000u, old, sizeof old);
    hw.fail = SR_PGSERR;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_h5_flash_update(&flash, 0x8000u, now, sizeof now));
    TEST_ASSERT_EQUAL_UINT(0u, hw.erases);
    TEST_ASSERT_EQUAL_UINT(0u, hw.programs);
    TEST_ASSERT_EQUAL_MEMORY(old, MEM + 0x8000u, sizeof old);
    assert_settled();
}

static void test_flags_an_earlier_operation_left_are_cleared_first(void)
{
    static const uint8_t field[4] = { 1u, 2u, 3u, 4u };

    regs[SR] = SR_PGSERR | SR_EOP;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_h5_flash_update(&flash, 0xFE000u, field, sizeof field));
    TEST_ASSERT_EQUAL_HEX32(SR_PGSERR | SR_EOP, hw.cleared & (SR_PGSERR | SR_EOP));
    TEST_ASSERT_EQUAL_UINT(1u, hw.programs);
    assert_settled();
}

static void test_data_that_does_not_read_back_is_internal(void)
{
    static const uint8_t field[4] = { 1u, 2u, 3u, 4u };

    hw.drop = 1;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL,
                          tfw_h5_flash_update(&flash, 0xFE000u, field, sizeof field));
    assert_settled();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_write_that_matches_writes_nothing);
    RUN_TEST(test_a_zero_length_write_writes_nothing);
    RUN_TEST(test_an_erased_page_is_programmed_without_an_erase);
    RUN_TEST(test_erased_quad_words_in_the_data_are_not_programmed);
    RUN_TEST(test_a_short_write_programs_the_quad_words_it_touches);
    RUN_TEST(test_the_key_is_written_only_to_a_locked_interface);
    RUN_TEST(test_a_programmed_page_rewrites_its_sector);
    RUN_TEST(test_a_field_beside_a_programmed_one_rewrites_the_sector);
    RUN_TEST(test_a_rewrite_that_loses_another_field_is_internal);
    RUN_TEST(test_the_stores_run_masked_and_the_mask_is_put_back);
    RUN_TEST(test_the_model_counts_stores_outside_the_mask);
    RUN_TEST(test_a_sector_in_bank_2_is_erased_through_bksel);
    RUN_TEST(test_a_write_across_two_sectors_rewrites_both);
    RUN_TEST(test_a_write_past_bank_2_is_out_of_range);
    RUN_TEST(test_an_interface_that_stays_locked_is_a_protected_region);
    RUN_TEST(test_a_write_protection_error_is_a_protected_region);
    RUN_TEST(test_a_programming_error_is_internal);
    RUN_TEST(test_an_erase_error_stops_the_rewrite);
    RUN_TEST(test_flags_an_earlier_operation_left_are_cleared_first);
    RUN_TEST(test_data_that_does_not_read_back_is_internal);
    return UNITY_END();
}
