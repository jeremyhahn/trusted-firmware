/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The M7's MPU region table (src/hal/cortex-m/hal_mpu.c, built with
 * TFW_CM_MPU_TABLE_ONLY): bases, sizes, attributes and alignment for the
 * application and for tfw-boot on mps2-an500's map, and the rejected
 * geometries.
 */
#include <stdint.h>

#include "unity.h"
#include "hal_mpu.h"

/* mps2-an500 (boards/mps2-an500/board_profile.h, profile.mk). */
#define AN500_BOOT_BASE  0x00000000uL
#define AN500_BOOT_SIZE  0x00008000uL
#define AN500_APP_BASE   0x00008000uL
#define AN500_APP_AB     0x00040000uL
#define AN500_HEAP       0x20000a44uL   /* an unaligned __heap_start */

#define RASR_NOACCESS(size_field) \
    (TFW_CM_MPU_RASR_XN | TFW_CM_MPU_RASR_SIZE(size_field) | TFW_CM_MPU_RASR_ENABLE)

static tfw_cm_mpu_region_t t[TFW_CM_MPU_REGIONS];

void setUp(void) { }
void tearDown(void) { }

static uint32_t base_of(uint32_t i)   { return t[i].rbar & ~0x1fu; }
static uint32_t bytes_of(uint32_t i)  { return 2u << ((t[i].rasr >> 1) & 0x1fu); }

static void assert_region_rules(uint32_t i)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MPU_RBAR_VALID | i, t[i].rbar & 0x1fu);
    if (t[i].rasr == 0u) {
        return;
    }
    TEST_ASSERT_EQUAL_HEX32(0u, (t[i].rasr >> 24) & 0x7u);           /* AP: no access */
    TEST_ASSERT_TRUE((t[i].rasr & TFW_CM_MPU_RASR_XN) != 0u);
    TEST_ASSERT_EQUAL_HEX32(0u, (t[i].rasr >> 8) & 0xffu);           /* no subregion off */
    TEST_ASSERT_TRUE(bytes_of(i) >= TFW_CM_MPU_GUARD);
    TEST_ASSERT_EQUAL_HEX32(0u, base_of(i) & (bytes_of(i) - 1u));    /* aligned to its size */
}

static void assert_all_rules(void)
{
    uint32_t i;

    for (i = 0u; i < TFW_CM_MPU_REGIONS; i++) {
        assert_region_rules(i);
    }
}

void test_app_table_has_all_three_guards(void)
{
    TEST_ASSERT_EQUAL_INT(0, tfw_cm_mpu_table(t, AN500_APP_BASE, AN500_HEAP,
                                              AN500_BOOT_BASE, AN500_BOOT_SIZE));
    assert_all_rules();

    TEST_ASSERT_EQUAL_HEX32(0x00000000uL | TFW_CM_MPU_RBAR_VALID | TFW_CM_MPU_NULL, t[TFW_CM_MPU_NULL].rbar);
    TEST_ASSERT_EQUAL_HEX32(RASR_NOACCESS(4u), t[TFW_CM_MPU_NULL].rasr);   /* 32 bytes */

    TEST_ASSERT_EQUAL_HEX32(0x20000c00uL | TFW_CM_MPU_RBAR_VALID | TFW_CM_MPU_STACK, t[TFW_CM_MPU_STACK].rbar);
    TEST_ASSERT_EQUAL_HEX32(RASR_NOACCESS(9u), t[TFW_CM_MPU_STACK].rasr);  /* 1 KiB */
    TEST_ASSERT_EQUAL_HEX32(TFW_CM_MPU_STACK_GUARD, bytes_of(TFW_CM_MPU_STACK));
    /* Larger than any frame the build lets through (hal_mpu.h). */
    TEST_ASSERT_TRUE(bytes_of(TFW_CM_MPU_STACK) >= TFW_CM_FRAME_MAX + TFW_CM_FRAME_MARGIN);
    TEST_ASSERT_TRUE(base_of(TFW_CM_MPU_STACK) >= AN500_HEAP);

    TEST_ASSERT_EQUAL_HEX32(AN500_BOOT_BASE | TFW_CM_MPU_RBAR_VALID | TFW_CM_MPU_BOOT, t[TFW_CM_MPU_BOOT].rbar);
    TEST_ASSERT_EQUAL_HEX32(RASR_NOACCESS(14u), t[TFW_CM_MPU_BOOT].rasr);  /* 32 KiB */
    TEST_ASSERT_EQUAL_HEX32(AN500_BOOT_SIZE, bytes_of(TFW_CM_MPU_BOOT));
    /* The application's own vectors stay outside the hidden region. */
    TEST_ASSERT_TRUE(AN500_APP_BASE >= base_of(TFW_CM_MPU_BOOT) + bytes_of(TFW_CM_MPU_BOOT));
}

void test_app_table_in_slot_b(void)
{
    TEST_ASSERT_EQUAL_INT(0, tfw_cm_mpu_table(t, AN500_APP_AB, AN500_HEAP,
                                              AN500_BOOT_BASE, AN500_BOOT_SIZE));
    assert_all_rules();
    TEST_ASSERT_NOT_EQUAL(0u, t[TFW_CM_MPU_NULL].rasr);
    TEST_ASSERT_NOT_EQUAL(0u, t[TFW_CM_MPU_BOOT].rasr);
}

void test_boot_table_keeps_its_own_region_and_vectors(void)
{
    TEST_ASSERT_EQUAL_INT(0, tfw_cm_mpu_table(t, AN500_BOOT_BASE, AN500_HEAP,
                                              AN500_BOOT_BASE, AN500_BOOT_SIZE));
    assert_all_rules();
    TEST_ASSERT_EQUAL_HEX32(0u, t[TFW_CM_MPU_NULL].rasr);   /* its vectors are at 0 */
    TEST_ASSERT_EQUAL_HEX32(0u, t[TFW_CM_MPU_BOOT].rasr);   /* its own code */
    TEST_ASSERT_EQUAL_HEX32(RASR_NOACCESS(9u), t[TFW_CM_MPU_STACK].rasr);
}

void test_aligned_stack_floor_is_the_guard_base(void)
{
    TEST_ASSERT_EQUAL_INT(0, tfw_cm_mpu_table(t, AN500_APP_BASE, 0x20001000uL,
                                              AN500_BOOT_BASE, AN500_BOOT_SIZE));
    TEST_ASSERT_EQUAL_HEX32(0x20001000uL, base_of(TFW_CM_MPU_STACK));
}

void test_no_stack_floor_no_stack_guard(void)
{
    TEST_ASSERT_EQUAL_INT(0, tfw_cm_mpu_table(t, AN500_APP_BASE, 0u,
                                              AN500_BOOT_BASE, AN500_BOOT_SIZE));
    assert_all_rules();
    TEST_ASSERT_EQUAL_HEX32(0u, t[TFW_CM_MPU_STACK].rasr);
}

void test_boot_region_away_from_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, tfw_cm_mpu_table(t, 0x00010000uL, AN500_HEAP,
                                              0x00100000uL, 0x00010000uL));
    assert_all_rules();
    TEST_ASSERT_EQUAL_HEX32(0x00100000uL, base_of(TFW_CM_MPU_BOOT));
    TEST_ASSERT_EQUAL_HEX32(0x00010000uL, bytes_of(TFW_CM_MPU_BOOT));
    /* An image inside that region is tfw-boot: nothing hidden. */
    TEST_ASSERT_EQUAL_INT(0, tfw_cm_mpu_table(t, 0x00100400uL, AN500_HEAP,
                                              0x00100000uL, 0x00010000uL));
    TEST_ASSERT_EQUAL_HEX32(0u, t[TFW_CM_MPU_BOOT].rasr);
}

void test_bad_boot_geometry_is_rejected(void)
{
    uint32_t i;

    TEST_ASSERT_EQUAL_INT(-1, tfw_cm_mpu_table(t, AN500_APP_BASE, AN500_HEAP, 0u, 0x6000uL));
    for (i = 0u; i < TFW_CM_MPU_REGIONS; i++) {
        TEST_ASSERT_EQUAL_HEX32(0u, t[i].rasr);
    }
    TEST_ASSERT_EQUAL_INT(-1, tfw_cm_mpu_table(t, AN500_APP_BASE, AN500_HEAP, 0x4000uL, 0x8000uL));
    TEST_ASSERT_EQUAL_INT(-1, tfw_cm_mpu_table(t, AN500_APP_BASE, AN500_HEAP, 0u, 16u));
    TEST_ASSERT_EQUAL_INT(-1, tfw_cm_mpu_table(t, AN500_APP_BASE, AN500_HEAP, 0u, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_app_table_has_all_three_guards);
    RUN_TEST(test_app_table_in_slot_b);
    RUN_TEST(test_boot_table_keeps_its_own_region_and_vectors);
    RUN_TEST(test_aligned_stack_floor_is_the_guard_base);
    RUN_TEST(test_no_stack_floor_no_stack_guard);
    RUN_TEST(test_boot_region_away_from_zero);
    RUN_TEST(test_bad_boot_geometry_is_rejected);
    return UNITY_END();
}
