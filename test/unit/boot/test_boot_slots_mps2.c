/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The A/B slot geometry of the MPS2 boards (boot/hal/cortex-m/boot_slots.h),
 * built once per board with its profile and TFW_CM_AB, as mk/boot.mk's A/B rows
 * build tfw-boot.
 *
 * The addresses are literals on purpose, as in test_boot_geometry.c: the header
 * derives the secondary from the primary, and a value worked out here from the
 * same macros would follow them wherever they went. QEMU loads the images at
 * these addresses (sdk/go/cmd/tfwctl/ota_ab_qemu_test.go), so a moved slot is
 * a moved image. Past the pins, each slot lies inside the part, clear of every
 * other region tfw-boot or QEMU places, and on the unit and the page.
 */

#include <stddef.h>

#include "unity.h"
#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_slots.h"

typedef struct {
    uint8_t  board_id;
    uint32_t primary;
    uint32_t secondary;
    uint32_t len;
    uint32_t unit;
} slots_t;

static const slots_t rows[] = {
    { 0x40u, 0x38040000uL, 0x38060200uL, 0x00020000uL, 512u },   /* mps2-an505 */
    { 0x42u, 0x00040000uL, 0x00060200uL, 0x00020000uL, 512u },   /* mps2-an500 */
};

typedef struct {
    const char *name;
    uint32_t    base;
    uint32_t    len;
} region_t;

/* Every span the A/B rows place: tfw-boot's, both slots, the store, the
 * platform block and the two factory copies QEMU loads. The primary is one unit
 * longer than the secondary (boot/boot_swap.c). */
static const region_t regions[] = {
    { "boot",              TFW_BOOT_REGION_BASE,     TFW_BOOT_REGION_SIZE },
    { "primary",           TFW_BOOT_SLOT_PRIMARY,    TFW_BOOT_SLOT_LEN + TFW_BOOT_SLOT_UNIT },
    { "secondary",         TFW_BOOT_SLOT_SECONDARY,  TFW_BOOT_SLOT_LEN },
    { "store",             TFW_OTA_STORE_MEM_BASE,   TFW_OTA_STORE_MEM_SIZE },
    { "platform",          TFW_CM_PLATFORM_BASE,     TFW_CM_PLATFORM_SIZE },
    { "factory app",       TFW_CM_LOAD_APP_AT,       TFW_BOOT_SLOT_LEN },
    { "factory platform",  TFW_CM_LOAD_PLATFORM_AT,  TFW_CM_PLATFORM_SIZE },
};

#define NREGIONS (sizeof regions / sizeof regions[0])

static const slots_t *row(void)
{
    size_t i = 0u;

    while (i < sizeof rows / sizeof rows[0] - 1u && rows[i].board_id != (uint8_t)TFW_BOARD_ID) {
        i++;
    }
    return &rows[i];
}

void setUp(void)    {}
void tearDown(void) {}

static void test_the_board_has_a_slot_row(void)
{
    TEST_ASSERT_EQUAL_HEX8((uint8_t)TFW_BOARD_ID, row()->board_id);
}

static void test_the_slots_are_the_pinned_ones(void)
{
    const slots_t *r = row();

    TEST_ASSERT_EQUAL_HEX32(r->primary, (uint32_t)TFW_BOOT_SLOT_PRIMARY);
    TEST_ASSERT_EQUAL_HEX32(r->secondary, (uint32_t)TFW_BOOT_SLOT_SECONDARY);
    TEST_ASSERT_EQUAL_HEX32(r->len, (uint32_t)TFW_BOOT_SLOT_LEN);
    TEST_ASSERT_EQUAL_UINT32(r->unit, (uint32_t)TFW_BOOT_SLOT_UNIT);
}

static void test_the_primary_is_the_application_link_base(void)
{
    TEST_ASSERT_EQUAL_HEX32((uint32_t)TFW_BOOT_APP_BASE, (uint32_t)TFW_BOOT_SLOT_PRIMARY);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)TFW_APP_REGION_SIZE, (uint32_t)TFW_BOOT_SLOT_LEN);
}

static void test_the_profile_names_the_derived_slots(void)
{
    TEST_ASSERT_EQUAL_HEX32((uint32_t)TFW_BOOT_SLOT_SECONDARY, (uint32_t)TFW_OTA_STAGE_DEST);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)TFW_BOOT_SLOT_END, (uint32_t)TFW_APP_WRITE_END);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(TFW_BOOT_SLOT_SECONDARY + TFW_BOOT_SLOT_LEN),
                            (uint32_t)TFW_BOOT_SLOT_END);
}

static void test_each_region_is_inside_the_part(void)
{
    for (size_t i = 0u; i < NREGIONS; i++) {
        TEST_ASSERT_TRUE_MESSAGE(regions[i].len > 0u, regions[i].name);
        TEST_ASSERT_TRUE_MESSAGE(regions[i].base <= (uint32_t)TFW_FLASH_TOTAL - regions[i].len,
                                 regions[i].name);
    }
}

static void test_no_two_regions_overlap(void)
{
    for (size_t i = 0u; i < NREGIONS; i++) {
        for (size_t j = i + 1u; j < NREGIONS; j++) {
            uint32_t a0 = regions[i].base, a1 = regions[i].base + regions[i].len;
            uint32_t b0 = regions[j].base, b1 = regions[j].base + regions[j].len;

            TEST_ASSERT_TRUE_MESSAGE(a1 <= b0 || b1 <= a0, regions[j].name);
        }
    }
}

static void test_the_slots_are_on_the_unit_and_the_page(void)
{
    uint32_t unit = (uint32_t)TFW_BOOT_SLOT_UNIT;
    uint32_t page = (uint32_t)TFW_OTA_PAGE_MAX;

    TEST_ASSERT_EQUAL_UINT32(0u, unit % 128u);
    TEST_ASSERT_EQUAL_UINT32(0u, unit % page);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)TFW_BOOT_SLOT_LEN % unit);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)TFW_BOOT_SLOT_PRIMARY % unit);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)TFW_BOOT_SLOT_SECONDARY % unit);
    TEST_ASSERT_TRUE((uint32_t)TFW_BOOT_SLOT_LEN / unit <= 256u);   /* the record's step byte */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_has_a_slot_row);
    RUN_TEST(test_the_slots_are_the_pinned_ones);
    RUN_TEST(test_the_primary_is_the_application_link_base);
    RUN_TEST(test_the_profile_names_the_derived_slots);
    RUN_TEST(test_each_region_is_inside_the_part);
    RUN_TEST(test_no_two_regions_overlap);
    RUN_TEST(test_the_slots_are_on_the_unit_and_the_page);
    return UNITY_END();
}
