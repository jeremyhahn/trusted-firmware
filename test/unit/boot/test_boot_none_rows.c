/*
 * The rows with no staging and no swap: boot_stage_none.c and
 * boot_swap_none.c, the stand-ins mk/boot.mk links where BOOT_STORE=0 or the
 * board has no second slot. Each answer is the one boot_stage.h and
 * boot_swap.h promise for "nothing here", and none of them touches the block.
 */
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_stage.h"
#include "boot_swap.h"
#include "boot_trial.h"

static tfw_boot_platform_t plat;
static tfw_boot_platform_t before;

void setUp(void)
{
    memset(&plat, 0, sizeof plat);
    plat.boot_state = TFW_BOOT_STATE_TRIAL;
    plat.ota_state  = TFW_BOOT_OTA_PENDING;
    before = plat;
}

void tearDown(void) {}

static void assert_block_untouched(void)
{
    TEST_ASSERT_EQUAL_MEMORY(&before, &plat, sizeof plat);
}

/* No store: nothing to take and nothing to forget, said as unsupported so
 * main() cannot read it as a staged image. */
static void test_the_stage_stand_in_is_unsupported(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(&plat, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_forget());
    tfw_boot_stage_after_reset(TFW_BOOT_RESET_FAIL, TFW_BOOT_RESET_BOOT);
    tfw_boot_stage_after_reset(TFW_BOOT_RESET_TRY, TFW_BOOT_RESET_TRY);
    assert_block_untouched();
}

/* No second slot: the action at reset passes through unchanged, whichever
 * it is. */
static void test_the_swap_stand_in_passes_the_reset_action_through(void)
{
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, tfw_boot_swap_at_reset(&plat, TFW_BOOT_RESET_BOOT));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_swap_at_reset(&plat, TFW_BOOT_RESET_FAIL));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, tfw_boot_swap_at_reset(&plat, TFW_BOOT_RESET_SETTLE));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_TRY, tfw_boot_swap_at_reset(&plat, TFW_BOOT_RESET_TRY));
    assert_block_untouched();
}

/* Take and commit are "nothing done", TFW_OK; there is nothing to fall back
 * to, so the fallback is refused. */
static void test_the_swap_stand_in_does_nothing_and_cannot_fall_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_commit(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_commit(&plat, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_swap_fall_back(&plat));
    assert_block_untouched();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_stage_stand_in_is_unsupported);
    RUN_TEST(test_the_swap_stand_in_passes_the_reset_action_through);
    RUN_TEST(test_the_swap_stand_in_does_nothing_and_cannot_fall_back);
    return UNITY_END();
}
