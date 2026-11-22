/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 SAU region table (#590), checked on the host. Built once per
 * board, with that board's tz_map_<board>.h passed by -include (mk/host.mk
 * test_tz_regions_<board>), so each board's table is the one its secure
 * image programs. The mutation cases feed tz_regions_check tables it must
 * refuse, so a checker that passes everything cannot pass this file.
 */
#include <string.h>
#include "unity.h"
#include "tz_regions.h"

#define NREG 4u

static struct tz_sau_region t[NREG];

void setUp(void)
{
    TEST_ASSERT_EQUAL_UINT(NREG, tz_sau_region_count);
    memcpy(t, tz_sau_regions, sizeof t);
}

void tearDown(void) {}

static enum tz_check check(void)
{
    return tz_regions_check(t, NREG, &tz_board_layout);
}

static const struct tz_sau_region *find(uint32_t base)
{
    size_t i;
    for (i = 0; i < tz_sau_region_count; i++)
        if (tz_sau_regions[i].base == base)
            return &tz_sau_regions[i];
    return NULL;
}

static void test_board_table_passes(void)
{
    TEST_ASSERT_EQUAL_INT(TZ_OK, tz_regions_check(tz_sau_regions, tz_sau_region_count, &tz_board_layout));
}

static void test_every_region_is_on_the_32_byte_granule(void)
{
    size_t i;
    for (i = 0; i < tz_sau_region_count; i++) {
        TEST_ASSERT_EQUAL_UINT32(0, tz_sau_regions[i].base % 32u);
        TEST_ASSERT_EQUAL_UINT32(0, (tz_sau_regions[i].limit + 1u) % 32u);
    }
}

static void test_nsc_ends_the_secure_code(void)
{
    const struct tz_sau_region *nsc = find(TZ_NSC_BASE);
    TEST_ASSERT_NOT_NULL(nsc);
    TEST_ASSERT_EQUAL_UINT8(1, nsc->nsc);
    TEST_ASSERT_EQUAL_HEX32(TZ_S_CODE_BASE + TZ_S_CODE_SIZE, TZ_NSC_BASE);
    TEST_ASSERT_EQUAL_HEX32(TZ_NSC_BASE + TZ_NSC_SIZE - 1u, nsc->limit);
}

static void test_ns_regions_are_the_maps(void)
{
    const struct tz_sau_region *code = find(TZ_NS_CODE_BASE);
    const struct tz_sau_region *ram = find(TZ_NS_RAM_BASE);
    const struct tz_sau_region *per = find(TZ_NS_PERIPH_BASE);
    TEST_ASSERT_NOT_NULL(code);
    TEST_ASSERT_NOT_NULL(ram);
    TEST_ASSERT_NOT_NULL(per);
    TEST_ASSERT_EQUAL_UINT8(0, code->nsc);
    TEST_ASSERT_EQUAL_UINT8(0, ram->nsc);
    TEST_ASSERT_EQUAL_UINT8(0, per->nsc);
    TEST_ASSERT_EQUAL_HEX32(TZ_NS_CODE_BASE + TZ_NS_CODE_SIZE - 1u, code->limit);
    TEST_ASSERT_EQUAL_HEX32(TZ_NS_RAM_BASE + TZ_NS_RAM_SIZE - 1u, ram->limit);
}

/* Each Secure region is on the IDAU's Secure alias, each NS one on the
 * Non-secure alias: the SAU cannot make an IDAU-Secure address NS. */
static void test_regions_are_on_their_idau_alias(void)
{
    TEST_ASSERT_TRUE((TZ_S_CODE_BASE & TZ_CODE_ALIAS_BIT) != 0);
    TEST_ASSERT_TRUE((TZ_NSC_BASE & TZ_CODE_ALIAS_BIT) != 0);
    TEST_ASSERT_TRUE((TZ_S_RAM_BASE & TZ_RAM_ALIAS_BIT) != 0);
    TEST_ASSERT_TRUE((TZ_NS_CODE_BASE & TZ_CODE_ALIAS_BIT) == 0);
    TEST_ASSERT_TRUE((TZ_NS_RAM_BASE & TZ_RAM_ALIAS_BIT) == 0);
}

static void test_refuses_a_misaligned_base(void)
{
    t[0].base += 16u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_ALIGN, check());
}

static void test_refuses_a_misaligned_limit(void)
{
    t[1].limit -= 8u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_ALIGN, check());
}

static void test_refuses_a_limit_below_its_base(void)
{
    t[0].limit = t[0].base - 1u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_BOUNDS, check());
}

static void test_refuses_overlap(void)
{
    t[1].base = t[0].limit + 1u - 32u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_OVERLAP, check());
}

static void test_refuses_nsc_outside_secure_code(void)
{
    t[3].base = TZ_NSC_BASE + TZ_NSC_SIZE;
    t[3].limit = t[3].base + 31u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_NSC_OUTSIDE, check());
}

static void test_refuses_nsc_over_secure_code(void)
{
    t[3].base = TZ_NSC_BASE - 32u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_NSC_OUTSIDE, check());
}

static void test_refuses_no_nsc(void)
{
    t[3].nsc = 0;
    t[3].base = TZ_NS_RAM_BASE + TZ_NS_RAM_SIZE;
    t[3].limit = t[3].base + 31u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_NSC_COUNT, check());
}

static void test_refuses_ns_over_secure_ram(void)
{
    t[1].base = TZ_S_RAM_BASE;
    t[1].limit = TZ_S_RAM_BASE + 31u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_SECURE_NS, check());
}

static void test_refuses_ns_over_secure_code(void)
{
    t[0].base = TZ_S_CODE_BASE;
    t[0].limit = TZ_S_CODE_BASE + 31u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_SECURE_NS, check());
}

static void test_refuses_nsc_made_ns(void)
{
    t[3].nsc = 0;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_SECURE_NS, check());
}

static void test_refuses_empty_and_too_many(void)
{
    TEST_ASSERT_EQUAL_INT(TZ_ERR_EMPTY, tz_regions_check(t, 0, &tz_board_layout));
    TEST_ASSERT_EQUAL_INT(TZ_ERR_EMPTY, tz_regions_check(t, TZ_SAU_MAX_REGIONS + 1u, &tz_board_layout));
}

/* tz_regions.c:57, the NSC's base below the Secure code's base. */
static void test_refuses_nsc_below_secure_code(void)
{
    t[3].base = TZ_S_CODE_BASE - 32u;
    t[3].limit = TZ_S_CODE_BASE - 1u;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_NSC_OUTSIDE, check());
}

/* tz_regions.c:43, a full SAU of TZ_SAU_MAX_REGIONS regions is not too many. */
static void test_accepts_exactly_max_regions(void)
{
    struct tz_sau_region u[TZ_SAU_MAX_REGIONS];
    uint32_t p = TZ_NS_PERIPH_BASE;
    size_t i;
    u[0] = t[0];
    u[1] = t[1];
    u[2] = t[3];
    for (i = 3; i < TZ_SAU_MAX_REGIONS - 1u; i++) {
        u[i].base = p;
        u[i].limit = p + 31u;
        u[i].nsc = 0;
        p += 32u;
    }
    u[i].base = p;
    u[i].limit = TZ_NS_PERIPH_BASE + TZ_NS_PERIPH_SIZE - 1u;
    u[i].nsc = 0;
    TEST_ASSERT_EQUAL_INT(TZ_OK, tz_regions_check(u, TZ_SAU_MAX_REGIONS, &tz_board_layout));
}

/* tz_regions.c:65, the NSC split into two NSC regions. */
static void test_refuses_two_nsc(void)
{
    struct tz_sau_region u[NREG + 1u];
    memcpy(u, t, sizeof t);
    u[3].limit = TZ_NSC_BASE + TZ_NSC_SIZE / 2u - 1u;
    u[4].base = TZ_NSC_BASE + TZ_NSC_SIZE / 2u;
    u[4].limit = TZ_NSC_BASE + TZ_NSC_SIZE - 1u;
    u[4].nsc = 1;
    TEST_ASSERT_EQUAL_INT(TZ_ERR_NSC_COUNT, tz_regions_check(u, NREG + 1u, &tz_board_layout));
}

/* tz_regions.c:30 and 40-41, NS regions next to each Secure range share no byte. */
static void test_accepts_ns_regions_touching_secure_ends(void)
{
    t[0].base = TZ_S_CODE_BASE - 32u;
    t[0].limit = TZ_S_CODE_BASE - 1u;
    t[1].base = TZ_S_RAM_BASE + TZ_S_RAM_SIZE;
    t[1].limit = t[1].base + 31u;
    TEST_ASSERT_EQUAL_INT(TZ_OK, check());
    t[0].base = TZ_NSC_BASE + TZ_NSC_SIZE;
    t[0].limit = t[0].base + 31u;
    TEST_ASSERT_EQUAL_INT(TZ_OK, check());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_board_table_passes);
    RUN_TEST(test_every_region_is_on_the_32_byte_granule);
    RUN_TEST(test_nsc_ends_the_secure_code);
    RUN_TEST(test_ns_regions_are_the_maps);
    RUN_TEST(test_regions_are_on_their_idau_alias);
    RUN_TEST(test_refuses_a_misaligned_base);
    RUN_TEST(test_refuses_a_misaligned_limit);
    RUN_TEST(test_refuses_a_limit_below_its_base);
    RUN_TEST(test_refuses_overlap);
    RUN_TEST(test_refuses_nsc_outside_secure_code);
    RUN_TEST(test_refuses_nsc_over_secure_code);
    RUN_TEST(test_refuses_no_nsc);
    RUN_TEST(test_refuses_ns_over_secure_ram);
    RUN_TEST(test_refuses_ns_over_secure_code);
    RUN_TEST(test_refuses_nsc_made_ns);
    RUN_TEST(test_refuses_empty_and_too_many);
    RUN_TEST(test_refuses_nsc_below_secure_code);
    RUN_TEST(test_accepts_exactly_max_regions);
    RUN_TEST(test_refuses_two_nsc);
    RUN_TEST(test_accepts_ns_regions_touching_secure_ends);
    return UNITY_END();
}
