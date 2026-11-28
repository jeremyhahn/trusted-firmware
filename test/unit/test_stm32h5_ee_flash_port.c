/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/cortex-m/stm32h5/ee_flash_port.c on the host, on the
 * stm32h573i-dk's profile: the part's flash interface and flash it hands
 * ee_flash.c. test_stm32h5_eeprom.c links its register model in place of this
 * file, so this is the file's own test. The port is included unchanged behind
 * stm32h5_ee_port_stubs.h, which stands in for cm_regs.h.
 */
#include <stdint.h>

#include "unity.h"
#include "../model/cortex-m/stm32h5_ee_port_stubs.h"
#include "stm32h5/soc_regs.h"

/* soc_regs.h's FLASH_NSSR offset, kept before the model takes the name. */
enum { NSSR_OFF = TFW_H5_FLASH_NSSR_OFF, NSSR = TFW_H5_FLASH_NSSR_OFF / 4u };
#define SR_HOLD   (TFW_H5_FLASH_SR_BSY | TFW_H5_FLASH_SR_WBNE | TFW_H5_FLASH_SR_DBNE)
#define RUNAWAY   1000u

/* The status register model: the wait's offset is evaluated on every poll,
 * so the port's TFW_H5_FLASH_NSSR_OFF goes through nssr_poll, which counts
 * the poll and clears each busy bit on its own poll number. No clock. */
static volatile uint32_t sr_regs[0x100u / 4u];
static unsigned polls;
static unsigned bsy_at, wbne_at, dbne_at;

static uint32_t nssr_poll(void)
{
    polls++;
    if (polls == bsy_at) {
        sr_regs[NSSR] &= ~TFW_H5_FLASH_SR_BSY;
    }
    if (polls == wbne_at) {
        sr_regs[NSSR] &= ~TFW_H5_FLASH_SR_WBNE;
    }
    if (polls == dbne_at) {
        sr_regs[NSSR] &= ~TFW_H5_FLASH_SR_DBNE;
    }
    if (polls >= RUNAWAY) {
        sr_regs[NSSR] &= ~SR_HOLD;
    }
    return NSSR_OFF;
}

#undef TFW_H5_FLASH_NSSR_OFF
#define TFW_H5_FLASH_NSSR_OFF (nssr_poll())

#include "../../src/hal/cortex-m/stm32h5/ee_flash_port.c"

uint32_t stub_primask;
unsigned stub_dsbs;
unsigned stub_saves;
unsigned stub_restores;
unsigned stub_dsbs_at_restore;

void setUp(void)
{
    stub_primask = 0u;
    stub_dsbs = 0u;
    stub_saves = 0u;
    stub_restores = 0u;
    stub_dsbs_at_restore = 0u;
    for (unsigned i = 0u; i < sizeof sr_regs / sizeof sr_regs[0]; i++) {
        sr_regs[i] = 0u;
    }
    polls = 0u;
    bsy_at = 0u;
    wbne_at = 0u;
    dbne_at = 0u;
}

void tearDown(void) {}

/* RM0481: the FLASH interface at 0x40022000, bank 1's first byte at
 * 0x08000000 (non-secure). */
static void test_the_port_names_the_flash_registers_and_the_flash(void)
{
    const struct tfw_h5_flash *f = tfw_h5_ee_flash();

    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_HEX32(0x40022000u, TFW_H5_FLASH_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x08000000u, TFW_H5_FLASH_MEM);
    TEST_ASSERT_EQUAL_PTR((void *)(uintptr_t)0x40022000u, (void *)(uintptr_t)f->regs);
    TEST_ASSERT_EQUAL_PTR((void *)(uintptr_t)0x08000000u, f->mem);
    TEST_ASSERT_EQUAL_PTR(f, tfw_h5_ee_flash());
}

/* The model's flash: the port's own interface, its registers on the model. */
static const struct tfw_h5_flash model = { sr_regs, 0 };

static void test_the_status_register_is_nssr_at_0x20(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x20u, NSSR_OFF);
}

static void test_the_wait_with_nothing_busy_polls_once(void)
{
    tfw_h5_flash_wait(&model);
    TEST_ASSERT_EQUAL_UINT(1u, polls);
}

/* BSY, WBNE and DBNE each clear on their own poll: the wait ends on the poll
 * that sees the last of them clear, whichever it is. */
static void test_the_wait_ends_only_once_bsy_wbne_and_dbne_are_clear(void)
{
    static const unsigned order[3][3] = { { 2u, 4u, 6u }, { 6u, 2u, 4u }, { 4u, 6u, 2u } };

    for (unsigned i = 0u; i < 3u; i++) {
        setUp();
        sr_regs[NSSR] = SR_HOLD;
        bsy_at = order[i][0];
        wbne_at = order[i][1];
        dbne_at = order[i][2];
        tfw_h5_flash_wait(&model);
        TEST_ASSERT_EQUAL_UINT(6u, polls);
        TEST_ASSERT_EQUAL_HEX32(0u, sr_regs[NSSR]);
    }
}

static void test_each_busy_bit_alone_holds_the_wait(void)
{
    static const uint32_t bit[3] = {
        TFW_H5_FLASH_SR_BSY, TFW_H5_FLASH_SR_WBNE, TFW_H5_FLASH_SR_DBNE
    };

    for (unsigned i = 0u; i < 3u; i++) {
        setUp();
        sr_regs[NSSR] = bit[i];
        bsy_at = 5u;
        wbne_at = 5u;
        dbne_at = 5u;
        tfw_h5_flash_wait(&model);
        TEST_ASSERT_EQUAL_UINT(5u, polls);
    }
}

/* EOP and the error flags are the caller's (h5_flash.c reads them after the
 * wait): set, they do not hold it. */
static void test_the_other_status_bits_do_not_hold_the_wait(void)
{
    sr_regs[NSSR] = ~SR_HOLD;
    tfw_h5_flash_wait(&model);
    TEST_ASSERT_EQUAL_UINT(1u, polls);
    TEST_ASSERT_EQUAL_HEX32(~SR_HOLD, sr_regs[NSSR]);
}

static void test_irq_save_masks_and_returns_the_mask_it_found(void)
{
    stub_primask = 0u;
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_h5_flash_irq_save());
    TEST_ASSERT_EQUAL_HEX32(1u, stub_primask);
    TEST_ASSERT_EQUAL_HEX32(1u, tfw_h5_flash_irq_save());
    TEST_ASSERT_EQUAL_UINT(2u, stub_saves);
}

/* An unmasked caller is unmasked again; a caller that was already masked
 * stays masked: the restore puts back the saved mask, not "on". */
static void test_irq_restore_restores_the_saved_mask(void)
{
    uint32_t outer, inner;

    outer = tfw_h5_flash_irq_save();
    inner = tfw_h5_flash_irq_save();
    tfw_h5_flash_irq_restore(inner);
    TEST_ASSERT_EQUAL_HEX32(1u, stub_primask);
    tfw_h5_flash_irq_restore(outer);
    TEST_ASSERT_EQUAL_HEX32(0u, stub_primask);
    TEST_ASSERT_EQUAL_UINT(2u, stub_restores);
}

/* The stores complete before the mask comes off: a DSB ahead of each restore. */
static void test_irq_restore_issues_a_dsb_before_the_mask(void)
{
    uint32_t m = tfw_h5_flash_irq_save();

    TEST_ASSERT_EQUAL_UINT(0u, stub_dsbs);
    tfw_h5_flash_irq_restore(m);
    TEST_ASSERT_EQUAL_UINT(1u, stub_dsbs);
    TEST_ASSERT_EQUAL_UINT(1u, stub_dsbs_at_restore);
}

/* tfw-boot calls in already masked: the restore keeps the mask on and still
 * ends the quad-word's stores with the DSB ahead of it. */
static void test_a_masked_callers_restore_still_issues_the_dsb(void)
{
    uint32_t m;

    stub_primask = 1u;
    m = tfw_h5_flash_irq_save();
    TEST_ASSERT_EQUAL_HEX32(1u, m);
    tfw_h5_flash_irq_restore(m);
    TEST_ASSERT_EQUAL_HEX32(1u, stub_primask);
    TEST_ASSERT_EQUAL_UINT(1u, stub_dsbs);
    TEST_ASSERT_EQUAL_UINT(1u, stub_restores);
    TEST_ASSERT_EQUAL_UINT(1u, stub_dsbs_at_restore);
}

/* The restore writes PRIMASK with the whole word the save read, not bit 0. */
static void test_the_restore_hands_back_the_saved_word_unchanged(void)
{
    uint32_t m;

    stub_primask = 0x80000001u;
    m = tfw_h5_flash_irq_save();
    TEST_ASSERT_EQUAL_HEX32(0x80000001u, m);
    TEST_ASSERT_EQUAL_HEX32(1u, stub_primask);
    tfw_h5_flash_irq_restore(m);
    TEST_ASSERT_EQUAL_HEX32(0x80000001u, stub_primask);
    TEST_ASSERT_EQUAL_UINT(1u, stub_restores);
}

/* RM0481 FLASH_NSSR: BSY is bit 0, WBNE bit 1, DBNE bit 3 (bit 2 is
 * reserved). The values are written here as literals, not through
 * soc_regs.h, so a moved bit fails. */
static void test_the_wait_holds_on_rm0481_bits_0_1_and_3(void)
{
    static const uint32_t bit[3] = { 0x00000001u, 0x00000002u, 0x00000008u };

    TEST_ASSERT_EQUAL_HEX32(0x0000000Bu, SR_HOLD);
    for (unsigned i = 0u; i < 3u; i++) {
        setUp();
        sr_regs[NSSR] = bit[i];
        bsy_at = 3u;
        wbne_at = 3u;
        dbne_at = 3u;
        tfw_h5_flash_wait(&model);
        TEST_ASSERT_EQUAL_UINT(3u, polls);
        TEST_ASSERT_EQUAL_HEX32(0u, sr_regs[NSSR]);
    }
}

/* Every bit but 0, 1 and 3 set, reserved bit 2 among them: the wait leaves
 * on its first poll and touches none of them. */
static void test_every_bit_but_0_1_and_3_leaves_the_wait_at_once(void)
{
    sr_regs[NSSR] = 0xFFFFFFF4u;
    tfw_h5_flash_wait(&model);
    TEST_ASSERT_EQUAL_UINT(1u, polls);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF4u, sr_regs[NSSR]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_port_names_the_flash_registers_and_the_flash);
    RUN_TEST(test_the_status_register_is_nssr_at_0x20);
    RUN_TEST(test_the_wait_with_nothing_busy_polls_once);
    RUN_TEST(test_the_wait_ends_only_once_bsy_wbne_and_dbne_are_clear);
    RUN_TEST(test_each_busy_bit_alone_holds_the_wait);
    RUN_TEST(test_the_other_status_bits_do_not_hold_the_wait);
    RUN_TEST(test_irq_save_masks_and_returns_the_mask_it_found);
    RUN_TEST(test_irq_restore_restores_the_saved_mask);
    RUN_TEST(test_irq_restore_issues_a_dsb_before_the_mask);
    RUN_TEST(test_a_masked_callers_restore_still_issues_the_dsb);
    RUN_TEST(test_the_restore_hands_back_the_saved_word_unchanged);
    RUN_TEST(test_the_wait_holds_on_rm0481_bits_0_1_and_3);
    RUN_TEST(test_every_bit_but_0_1_and_3_leaves_the_wait_at_once);
    return UNITY_END();
}
