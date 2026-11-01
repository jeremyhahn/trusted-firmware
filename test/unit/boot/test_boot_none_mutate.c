/*
 * The rows with no staging and no swap (boot_stage_none.c, boot_swap_none.c),
 * driven by a seeded mutation loop: whatever the platform block holds and
 * whichever floor and action they are handed, the stand-ins return the one
 * status their header names and write nothing.
 *
 * Linked with the two stand-ins alone, no boot HAL: a stand-in that reached
 * the store's bus, the flash or the platform's write would not link.
 *
 * Deterministic: xorshift32 from a literal seed, a literal iteration count.
 *
 * Mutants this file kills (each tried by hand against the two units):
 *   - stage_take returns TFW_OK, TFW_ERR_BAD_STATE, or a status that depends
 *     on the floor (floor == 0, floor > 0, floor == ~0)
 *   - stage_forget returns TFW_OK
 *   - stage_after_reset or any swap stand-in writes boot_state, ota_state or
 *     any other byte of the block
 *   - swap_at_reset returns a fixed action, FAIL mapped to BOOT, or an action
 *     changed by the block's state
 *   - swap_take / swap_commit return anything but TFW_OK, or depend on floor
 *   - swap_fall_back returns TFW_OK (a fallback with nothing to fall back to)
 *   - any stand-in that dereferences its platform pointer (NULL accepted)
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_stage.h"
#include "boot_swap.h"
#include "boot_trial.h"

#define ITERATIONS 2000u

static uint32_t rng;
static tfw_boot_platform_t plat;
static tfw_boot_platform_t before;

static uint32_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* Every byte of the block at random: states the row never stores included,
 * so a stand-in that read one would be seen to answer differently. */
static void fill_block(void)
{
    uint8_t *b = (uint8_t *)&plat;
    size_t i;

    for (i = 0; i < sizeof plat; i++) {
        b[i] = (uint8_t)next_rand();
    }
    before = plat;
}

static tfw_boot_reset_action_t any_action(void)
{
    return (tfw_boot_reset_action_t)(next_rand() % 4u);
}

void setUp(void)
{
    rng = 0x1630C0DEu;
    memset(&plat, 0, sizeof plat);
    before = plat;
}

void tearDown(void) {}

static void test_stage_take_is_unsupported_for_any_block_and_floor(void)
{
    uint32_t i;

    for (i = 0; i < ITERATIONS; i++) {
        uint32_t floor = next_rand();

        fill_block();
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(&plat, floor));
        TEST_ASSERT_EQUAL_MEMORY(&before, &plat, sizeof plat);
    }
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(&plat, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(&plat, 0xFFFFFFFFu));
}

/* main() reads OK as "a staged image was copied": no other status may be
 * read that way, and BAD_STATE would say a MARK was there to refuse. */
static void test_stage_answers_are_not_ok_and_not_bad_state(void)
{
    tfw_status_t take = tfw_boot_stage_take(&plat, 0u);
    tfw_status_t forget = tfw_boot_stage_forget();

    TEST_ASSERT_NOT_EQUAL(TFW_OK, take);
    TEST_ASSERT_NOT_EQUAL(TFW_ERR_BAD_STATE, take);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, forget);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, forget);
}

static void test_stage_after_reset_writes_nothing_for_every_pair(void)
{
    int trial;
    int act;

    fill_block();
    for (trial = 0; trial < 4; trial++) {
        for (act = 0; act < 4; act++) {
            tfw_boot_stage_after_reset((tfw_boot_reset_action_t)trial,
                                       (tfw_boot_reset_action_t)act);
            TEST_ASSERT_EQUAL_MEMORY(&before, &plat, sizeof plat);
        }
    }
    /* Still no store after any of them. */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_forget());
}

static void test_swap_at_reset_is_the_identity_on_any_block(void)
{
    uint32_t i;

    for (i = 0; i < ITERATIONS; i++) {
        tfw_boot_reset_action_t act = any_action();

        fill_block();
        TEST_ASSERT_EQUAL(act, tfw_boot_swap_at_reset(&plat, act));
        TEST_ASSERT_EQUAL_MEMORY(&before, &plat, sizeof plat);
    }
}

static void test_swap_take_and_commit_are_ok_for_any_block_and_floor(void)
{
    uint32_t i;

    for (i = 0; i < ITERATIONS; i++) {
        uint32_t floor = next_rand();

        fill_block();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, floor));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_commit(&plat, floor));
        TEST_ASSERT_EQUAL_MEMORY(&before, &plat, sizeof plat);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_commit(&plat, 1u));
}

static void test_swap_fall_back_is_refused_for_any_block(void)
{
    uint32_t i;

    for (i = 0; i < ITERATIONS; i++) {
        fill_block();
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_swap_fall_back(&plat));
        TEST_ASSERT_EQUAL_MEMORY(&before, &plat, sizeof plat);
    }
}

/* The stand-ins never look at the block: a NULL one is answered the same. */
static void test_the_stand_ins_never_dereference_the_block(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_swap_at_reset(NULL, TFW_BOOT_RESET_FAIL));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_TRY, tfw_boot_swap_at_reset(NULL, TFW_BOOT_RESET_TRY));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_commit(NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_swap_fall_back(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stage_take_is_unsupported_for_any_block_and_floor);
    RUN_TEST(test_stage_answers_are_not_ok_and_not_bad_state);
    RUN_TEST(test_stage_after_reset_writes_nothing_for_every_pair);
    RUN_TEST(test_swap_at_reset_is_the_identity_on_any_block);
    RUN_TEST(test_swap_take_and_commit_are_ok_for_any_block_and_floor);
    RUN_TEST(test_swap_fall_back_is_refused_for_any_block);
    RUN_TEST(test_the_stand_ins_never_dereference_the_block);
    return UNITY_END();
}
