/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The factory copy of the two-slot MPS2 rows (boot/hal/cortex-m/boot_factory.c,
 * #628): a blank home takes the factory image QEMU loaded beside it, and a
 * programmed home is never touched.
 *
 * tfw_cm_boot_factory reads and writes the mps2-an505 BOOT_AB=1 profile's
 * fixed addresses, so the test backs them with host memory mapped at exactly
 * those addresses (MAP_FIXED_NOREPLACE). A mapping the host refuses fails the
 * test: a skip would pass with nothing checked. tfw_cm_dsb comes through the
 * rp2350 test cm_regs.h and is counted here.
 */
#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "unity.h"
#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_slots.h"
#include "boot_soc.h"
#include "cm_regs.h"

/* Bytes checked past the end of each home: the copy stops at its length. */
#define GUARD     64u
#define SENTINEL  0xEEu

#define SLOT_AT   ((uintptr_t)TFW_BOOT_SLOT_PRIMARY)
#define PLAT_AT   ((uintptr_t)(TFW_CM_PLATFORM_BASE + TFW_BOOT_EE_BASE))
#define FAPP_AT   ((uintptr_t)TFW_CM_LOAD_APP_AT)
#define FPLT_AT   ((uintptr_t)(TFW_CM_LOAD_PLATFORM_AT + TFW_BOOT_EE_BASE))
#define SLOT_LEN  ((size_t)TFW_BOOT_SLOT_LEN)
#define PLAT_LEN  ((size_t)TFW_BOOT_EE_LEN)

static uint8_t *const slot = (uint8_t *)SLOT_AT;
static uint8_t *const plat = (uint8_t *)PLAT_AT;
static uint8_t *const fapp = (uint8_t *)FAPP_AT;
static uint8_t *const fplt = (uint8_t *)FPLT_AT;

static uintptr_t span_lo;
static size_t    span_len;
static int       mapped;

static uint8_t   snap_slot[TFW_BOOT_SLOT_LEN];
static uint8_t   snap_plat[TFW_BOOT_EE_LEN];
static unsigned  dsb_calls;

void tfw_cm_dsb(void)
{
    dsb_calls++;
}

static uintptr_t min_at(uintptr_t a, uintptr_t b) { return a < b ? a : b; }
static uintptr_t max_at(uintptr_t a, uintptr_t b) { return a > b ? a : b; }

/* One span over the four regions and the guards, on the host's pages. */
static void map_profile(void)
{
    uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t lo = min_at(min_at(SLOT_AT, PLAT_AT), min_at(FAPP_AT, FPLT_AT));
    uintptr_t hi = max_at(max_at(SLOT_AT + SLOT_LEN, PLAT_AT + PLAT_LEN),
                          max_at(FAPP_AT + SLOT_LEN, FPLT_AT + PLAT_LEN)) + GUARD;
    void     *p;

    lo &= ~(page - 1u);
    hi = (hi + page - 1u) & ~(page - 1u);
    p = mmap((void *)lo, (size_t)(hi - lo), PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    /* A kernel before 4.17 takes the flag as a hint and maps elsewhere. */
    if (p == MAP_FAILED) {
        return;
    }
    if ((uintptr_t)p != lo) {
        (void)munmap(p, (size_t)(hi - lo));
        return;
    }
    span_lo  = lo;
    span_len = (size_t)(hi - lo);
    mapped   = 1;
}

static uint8_t app_byte(size_t i) { return (uint8_t)(i % 251u + 1u); }
static uint8_t plt_byte(size_t i) { return (uint8_t)(i % 239u + 3u); }

/* QEMU's RAM starts zeroed: both homes blank, the factory copies loaded,
 * and a sentinel past each home. */
void setUp(void)
{
    size_t i;

    if (mapped == 0) {
        return;
    }
    memset((void *)span_lo, 0, span_len);
    for (i = 0u; i < SLOT_LEN; i++) {
        fapp[i] = app_byte(i);
    }
    for (i = 0u; i < PLAT_LEN; i++) {
        fplt[i] = plt_byte(i);
    }
    memset(slot + SLOT_LEN, SENTINEL, GUARD);
    memset(plat + PLAT_LEN, SENTINEL, GUARD);
    dsb_calls = 0u;
}

void tearDown(void) {}

static void snapshot(void)
{
    memcpy(snap_slot, slot, SLOT_LEN);
    memcpy(snap_plat, plat, PLAT_LEN);
}

static void assert_guards_kept(void)
{
    size_t i;

    for (i = 0u; i < GUARD; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(SENTINEL, slot[SLOT_LEN + i], "past the slot");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(SENTINEL, plat[PLAT_LEN + i], "past the platform block");
    }
}

static void assert_factory_kept(void)
{
    size_t i;

    for (i = 0u; i < SLOT_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(app_byte(i), fapp[i]);
    }
    for (i = 0u; i < PLAT_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(plt_byte(i), fplt[i]);
    }
}

static void test_the_profile_addresses_are_mapped(void)
{
    TEST_ASSERT_TRUE_MESSAGE(mapped != 0,
                             "the host refused a fixed mapping at the mps2-an505 addresses");
}

static void test_a_blank_home_takes_both_factory_images(void)
{
    tfw_cm_boot_factory();

    TEST_ASSERT_EQUAL_MEMORY(fapp, slot, SLOT_LEN);
    TEST_ASSERT_EQUAL_MEMORY(fplt, plat, PLAT_LEN);
    assert_guards_kept();
    assert_factory_kept();
    TEST_ASSERT_EQUAL_UINT(1u, dsb_calls);
}

static void test_a_programmed_slot_is_never_touched(void)
{
    size_t i;

    for (i = 0u; i < SLOT_LEN; i++) {
        slot[i] = (uint8_t)(i % 13u);
    }
    slot[3] = 0x20u;   /* a stack top, 0x20xxxxxx: never zero in an image */
    snapshot();

    tfw_cm_boot_factory();

    TEST_ASSERT_EQUAL_MEMORY(snap_slot, slot, SLOT_LEN);
    TEST_ASSERT_EQUAL_MEMORY(fplt, plat, PLAT_LEN);
    assert_guards_kept();
    TEST_ASSERT_EQUAL_UINT(1u, dsb_calls);
}

static void keeps_a_platform_block_set_at(size_t at)
{
    plat[at] = 0x01u;
    snapshot();

    tfw_cm_boot_factory();

    TEST_ASSERT_EQUAL_MEMORY(snap_plat, plat, PLAT_LEN);
    TEST_ASSERT_EQUAL_MEMORY(fapp, slot, SLOT_LEN);
    assert_guards_kept();
    TEST_ASSERT_EQUAL_UINT(1u, dsb_calls);
}

static void test_a_platform_block_with_its_first_byte_set_is_kept(void)
{
    keeps_a_platform_block_set_at(0u);
}

static void test_a_platform_block_with_its_last_byte_set_is_kept(void)
{
    keeps_a_platform_block_set_at(PLAT_LEN - 1u);
}

/* The design: the slot is blank when its stack-top word is, since no image
 * has a zero there (the source's comment). Whatever follows a zero first word
 * is not an image, and the factory copy replaces it. */
static void test_the_slot_blank_test_reads_only_the_stack_top(void)
{
    slot[4] = 0x5Au;
    slot[SLOT_LEN / 2u] = 0x5Au;
    slot[SLOT_LEN - 1u] = 0x5Au;

    tfw_cm_boot_factory();

    TEST_ASSERT_EQUAL_MEMORY(fapp, slot, SLOT_LEN);
    TEST_ASSERT_EQUAL_MEMORY(fplt, plat, PLAT_LEN);
    assert_guards_kept();
    TEST_ASSERT_EQUAL_UINT(1u, dsb_calls);
}

static void test_both_homes_programmed_are_both_kept(void)
{
    slot[0] = 0x01u;
    plat[PLAT_LEN / 2u] = 0x01u;
    snapshot();

    tfw_cm_boot_factory();

    TEST_ASSERT_EQUAL_MEMORY(snap_slot, slot, SLOT_LEN);
    TEST_ASSERT_EQUAL_MEMORY(snap_plat, plat, PLAT_LEN);
    assert_guards_kept();
    TEST_ASSERT_EQUAL_UINT(1u, dsb_calls);
}

int main(void)
{
    map_profile();
    UNITY_BEGIN();
    RUN_TEST(test_the_profile_addresses_are_mapped);
    if (mapped == 0) {
        return UNITY_END();
    }
    RUN_TEST(test_a_blank_home_takes_both_factory_images);
    RUN_TEST(test_a_programmed_slot_is_never_touched);
    RUN_TEST(test_a_platform_block_with_its_first_byte_set_is_kept);
    RUN_TEST(test_a_platform_block_with_its_last_byte_set_is_kept);
    RUN_TEST(test_the_slot_blank_test_reads_only_the_stack_top);
    RUN_TEST(test_both_homes_programmed_are_both_kept);
    return UNITY_END();
}
