/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/boot_prev.c: the 328P's and 32U4's fallback (issue #52) -- PREV copied
 * back from the external store, against the software NOR part on the mock
 * boot HAL's store bus, under the host board's (the 328P's) profile.
 *
 * What the restored image does next is main()'s: evaluated as every boot,
 * and refused, recovery. Here, that the right bytes land, in the right
 * order with the block, and only when there is something to fall back from.
 */
#include <limits.h>
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "boot_swap.h"
#include "boot_trial.h"
#include "../../model/spi_nor_model.h"

#define PREV ((uint32_t)TFW_OTA_STORE_PREV_ADDR)

static uint8_t             prev[TFW_BOOT_APP_SIZE];
static tfw_boot_platform_t plat;
static unsigned            stores;
static tfw_boot_platform_t stored;
static int                 stored_flags;   /* -1: the flags byte not written */

/* The block's write, the test's: boot_store.c stays out of the link. */
void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    stored = *p;
    stores++;
}

void tfw_boot_platform_store_flags(const tfw_boot_platform_t *p, uint8_t flags)
{
    tfw_boot_platform_store(p);
    stored_flags = flags;
}

/* The image before the round, laid out in PREV as the region: its bytes,
 * then the trailer's bytes at the region's end. Not a valid image -- the copy
 * does not judge it; main()'s evaluation does. */
static void lay_prev(void)
{
    uint32_t i;

    for (i = 0u; i < sizeof prev; i++) {
        prev[i] = (uint8_t)(i * 5u + 1u);
    }
    tfw_nor_model_load(PREV, prev, sizeof prev);
}

static void assert_restored(void)
{
    const uint8_t *flash = tfw_boot_mock_flash();

    TEST_ASSERT_EQUAL_MEMORY(prev, &flash[TFW_BOOT_APP_BASE], sizeof prev);
    TEST_ASSERT_EQUAL_UINT(1u, stores);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

static void assert_nothing_restored(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT(0u, stores);
}

void setUp(void)
{
    tfw_boot_mock_reset();
    tfw_nor_model_reset();
    tfw_boot_mock_on_store(tfw_nor_model_xfer);
    memset(&plat, 0, sizeof plat);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_PENDING;
    stores = 0u;
    stored_flags = -1;
    lay_prev();
}

void tearDown(void) {}

static void test_a_trial_found_at_reset_restores_prev_and_boots(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT,
                      tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat)));
    assert_restored();
    /* The trial ended unconfirmed: fell back, not refused (ADR decision 8,
     * OTA_INFO flags bit 1 clear), whatever an earlier round left. */
    TEST_ASSERT_EQUAL_INT(0, stored_flags);
}

static void test_a_refused_pending_image_restores_prev(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_fall_back(&plat));
    assert_restored();
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_EE_FLAG_REFUSED, stored_flags);
}

/* Nothing on trial and nothing pending: a confirmed image settling, an
 * ordinary boot, and the restored PREV itself refused at evaluation -- that
 * last one halts in main(), and is never copied over again. */
static void test_nothing_to_fall_back_from_copies_nothing(void)
{
    static const uint8_t ota[] = { TFW_BOOT_OTA_IDLE, TFW_BOOT_OTA_FAILED };
    unsigned             i;

    for (i = 0u; i < sizeof ota; i++) {
        plat.boot_state = TFW_BOOT_STATE_GOOD;
        plat.ota_state  = ota[i];
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_swap_fall_back(&plat));
        TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT,
                          tfw_boot_swap_at_reset(&plat, TFW_BOOT_RESET_BOOT));
    }
    plat.boot_state = TFW_BOOT_STATE_CONFIRMED;
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE,
                      tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat)));
    assert_nothing_restored();
}

/* A reset mid-copy: nothing stored, so the next reset finds the trial again
 * and copies PREV again from the start. */
static void test_a_restore_cut_short_is_redone_at_the_next_reset(void)
{
    tfw_boot_platform_t at_reset;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    at_reset = plat;
    tfw_boot_mock_cut_after(5u);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL,
                      tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat)));
    TEST_ASSERT_EQUAL_UINT(0u, stores);

    plat = at_reset;
    tfw_boot_mock_cut_after(UINT_MAX);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT,
                      tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat)));
    assert_restored();
}

/* A store that reads as absent: no page is written from it, nothing stored. */
static void test_a_store_that_does_not_answer_restores_nothing(void)
{
    tfw_boot_mock_on_store(NULL);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_swap_fall_back(&plat));
    assert_nothing_restored();
}

/* The 328P has no secondary slot: take and commit are boot_swap.h's "nothing
 * done" answer, TFW_OK with no page written and no state stored, whatever is
 * pending (boot_prev.c, tfw_boot_swap_take and tfw_boot_swap_commit). */
static void test_take_and_commit_do_nothing_without_the_slots(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_commit(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_commit(&plat, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL(TFW_BOOT_OTA_PENDING, plat.ota_state);
    assert_nothing_restored();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_trial_found_at_reset_restores_prev_and_boots);
    RUN_TEST(test_a_refused_pending_image_restores_prev);
    RUN_TEST(test_nothing_to_fall_back_from_copies_nothing);
    RUN_TEST(test_a_restore_cut_short_is_redone_at_the_next_reset);
    RUN_TEST(test_a_store_that_does_not_answer_restores_nothing);
    RUN_TEST(test_take_and_commit_do_nothing_without_the_slots);
    return UNITY_END();
}
