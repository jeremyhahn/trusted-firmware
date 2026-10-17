/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/boot_stage.c: tfw-boot takes the image the application staged in the
 * external store (issue #75) -- against the software NOR part on the mock
 * boot HAL's store bus.
 *
 * Built twice (mk/boot.mk): for the host board, whose STAGE is the whole
 * application region, and as test_boot_stage_2560 with the 2560's profile,
 * whose STAGE is 64 KB of a 248 KB region -- so the trailer that ends the
 * staged stream is copied far past the end of what STAGE holds.
 */
#include <limits.h>
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "boot_measure.h"
#include "boot_stage.h"
#include "boot_store.h"
#include "boot_trial.h"
#include "../../model/spi_nor_model.h"

/* Where a staged image lands: the secondary slot on the 2560, the region
 * itself on the host board. */
#define DEST(a) ((a) - TFW_BOOT_APP_BASE + (uint32_t)TFW_OTA_STAGE_DEST)

#define STAGE   ((uint32_t)TFW_OTA_STORE_STAGE_ADDR)
#define MARK    ((uint32_t)TFW_OTA_STORE_MARK_ADDR)
#define VERSION 7u

static uint8_t             stream[TFW_OTA_STORE_STAGE_LEN];
static uint8_t             trailer[TFW_BOOT_TRAILER_LEN];
static tfw_boot_platform_t plat;
static unsigned            stores;
static uint8_t             stored_states[8];

/* The platform block's write, recorded rather than kept: what matters here is
 * the order of states the copy stores. */
void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    stored_states[stores % sizeof stored_states] = p->boot_state;
    stores++;
}

static const uint8_t *src;

static tfw_status_t mem_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    memcpy(buf, &src[addr], len);
    return TFW_OK;
}

static void wr32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

/* What a stager leaves: `total` bytes at STAGE -- the image padded to
 * total - 128, then its trailer -- and, if `marked`, the record naming them. */
static void stage(uint32_t image_len, uint32_t total, uint8_t board, int marked)
{
    uint32_t body = total - TFW_BOOT_TRAILER_LEN;
    uint8_t  digest[TFW_BOOT_MEASURE_LEN];
    uint8_t  mark[TFW_STAGE_MARK_LEN];
    uint32_t i;

    for (i = 0u; i < body; i++) {
        stream[i] = (uint8_t)(i * 7u + 3u);
    }
    src = stream;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(mem_read, 0u, image_len, digest));

    memset(trailer, 0, sizeof trailer);
    wr32(&trailer[TFW_BOOT_TR_MAGIC], TFW_BOOT_TRAILER_MAGIC);
    trailer[TFW_BOOT_TR_VERSION] = (uint8_t)TFW_BOOT_TRAILER_VERSION;
    trailer[TFW_BOOT_TR_BOARD_ID] = board;
    wr32(&trailer[TFW_BOOT_TR_IMAGE_VERSION], VERSION);
    wr32(&trailer[TFW_BOOT_TR_IMAGE_LEN], image_len);
    memcpy(&trailer[TFW_BOOT_TR_MEASUREMENT], digest, sizeof digest);
    memset(&trailer[TFW_BOOT_TR_SIGNATURE], 0x5A, TFW_BOOT_SIG_LEN);
    memcpy(&stream[body], trailer, sizeof trailer);
    tfw_nor_model_load(STAGE, stream, total);

    if (marked) {
        wr32(mark, TFW_STAGE_MARK_MAGIC);
        wr32(&mark[TFW_STAGE_MARK_O_LEN], total);
        memcpy(&mark[TFW_STAGE_MARK_O_HASH], digest, sizeof digest);
        tfw_nor_model_load(MARK, mark, sizeof mark);
    }
}

static void assert_installed(uint32_t total)
{
    const uint8_t *flash = tfw_boot_mock_flash();
    uint32_t       body  = total - TFW_BOOT_TRAILER_LEN;

    TEST_ASSERT_EQUAL_MEMORY(stream, &flash[DEST(TFW_BOOT_APP_BASE)], body);
    TEST_ASSERT_EQUAL_MEMORY(trailer, &flash[DEST(TFW_BOOT_TRAILER_ADDR)], TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_PENDING, plat.ota_state);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

static void assert_nothing_copied(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT(0u, stores);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_IDLE, plat.ota_state);
}

void setUp(void)
{
    tfw_boot_mock_reset();
    tfw_nor_model_reset();
    tfw_boot_mock_on_store(tfw_nor_model_xfer);
    memset(&plat, 0, sizeof plat);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_IDLE;
    stores = 0u;
}

void tearDown(void) {}

static tfw_status_t flash_rd(uint32_t addr, uint8_t *buf, uint8_t len)
{
    tfw_boot_flash_read(addr, buf, len);
    return TFW_OK;
}

static void test_a_valid_image_copies_and_enters_its_trial(void)
{
    uint8_t digest[TFW_BOOT_MEASURE_LEN];

    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, VERSION));
    assert_installed(1200u);
    TEST_ASSERT_EQUAL_UINT(2u, stores);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_STAGED, stored_states[0]);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored_states[1]);

    /* The copy now measures as the trailer says, from where it landed. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(flash_rd, DEST(TFW_BOOT_APP_BASE),
                                                    1000u, digest));
    TEST_ASSERT_EQUAL_MEMORY(&trailer[TFW_BOOT_TR_MEASUREMENT], digest, sizeof digest);

    /* #82's path from here: armed for its one trial. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, plat.boot_state);
}

static void test_a_bad_hash_copies_nothing(void)
{
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    tfw_nor_model_corrupt(STAGE + 500u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_stage_take(&plat, 0u));
    assert_nothing_copied();
}

static void test_a_wrong_board_id_copies_nothing(void)
{
    stage(1000u, 1200u, (uint8_t)(TFW_BOARD_ID ^ 0x40u), 1);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, 0u));
    assert_nothing_copied();
}

static void test_a_missing_mark_copies_nothing(void)
{
    stage(1000u, 1200u, TFW_BOARD_ID, 0);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_stage_take(&plat, 0u));
    assert_nothing_copied();
}

static void test_an_image_below_the_floor_copies_nothing(void)
{
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_boot_stage_take(&plat, VERSION + 1u));
    assert_nothing_copied();
}

static void test_no_new_round_over_a_pending_image(void)
{
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    plat.ota_state = TFW_BOOT_OTA_PENDING;
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
}

static void test_an_installed_image_is_not_copied_again(void)
{
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, 0u));
    plat.ota_state = TFW_BOOT_OTA_IDLE;   /* tried, confirmed and settled */
    stores = 0u;
    tfw_boot_mock_cut_after(0u);          /* any write at all now fails */
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_stage_take(&plat, VERSION));
    TEST_ASSERT_EQUAL_UINT(0u, stores);
}

static void test_a_copy_cut_at_a_chosen_page_recovers_on_the_next_boot(void)
{
    const uint8_t *flash = tfw_boot_mock_flash();
    uint32_t       total = 4000u;

    stage(3800u, total, TFW_BOARD_ID, 1);
    tfw_boot_mock_cut_after(5u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(5u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_STAGED, plat.boot_state);
    /* The trailer page goes last, so the region holds no trailer for it. */
    TEST_ASSERT_TRUE(memcmp(trailer, &flash[DEST(TFW_BOOT_TRAILER_ADDR)], TFW_BOOT_TRAILER_LEN) != 0);

    /* The next boot: STAGED, power back, the store untouched. */
    tfw_boot_mock_cut_after(UINT_MAX);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, 0u));
    assert_installed(total);
}

/* The image that ends exactly at STAGE's end. On the 2560 that is a whole
 * 64 KB slot, copied into the secondary; on the host board STAGE's end and
 * the region's are the same address. */
static void test_an_image_ending_at_the_end_of_stage(void)
{
    const uint8_t *flash = tfw_boot_mock_flash();
    uint32_t       total = (uint32_t)TFW_OTA_STORE_STAGE_LEN;
    uint32_t       body  = total - TFW_BOOT_TRAILER_LEN;

    stage(body, total, TFW_BOARD_ID, 1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, 0u));
    assert_installed(total);
    if (body < TFW_BOOT_MAX_IMAGE_LEN) {
        TEST_ASSERT_EQUAL_HEX8(0xFFu, flash[DEST(TFW_BOOT_APP_BASE + body)]);
        TEST_ASSERT_EQUAL_HEX8(0xFFu, flash[DEST(TFW_BOOT_TRAILER_ADDR - 1u)]);
    }
}

/* Item 56: the block as a fallback leaves it, GOOD with FAILED, which permits
 * a new round, and a region that no longer holds the staged image. Without
 * the clear the next reset copies the failed image in again; with it, the
 * MARK is gone and nothing is taken. */
static void test_a_fallback_clears_the_mark_and_the_next_reset_takes_nothing(void)
{
    const uint8_t *mark;

    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    plat.ota_state = TFW_BOOT_OTA_FAILED;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_forget());
    mark = &tfw_nor_model_mem()[MARK];
    TEST_ASSERT_EQUAL_HEX8(0u, mark[0] | mark[1] | mark[2] | mark[3]);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_stage_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT(0u, stores);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, plat.ota_state);
}

/* The control: the same block and store, not cleared, takes the image. */
static void test_an_uncleared_mark_after_a_fallback_is_taken_again(void)
{
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    plat.ota_state = TFW_BOOT_OTA_FAILED;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, 0u));
    assert_installed(1200u);
}

/* A store that does not answer: the clear fails, and writes nothing. */
static void test_a_clear_on_a_silent_store_fails(void)
{
    tfw_boot_mock_on_store(NULL);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_stage_forget());
    TEST_ASSERT_EQUAL_UINT(0u, stores);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_valid_image_copies_and_enters_its_trial);
    RUN_TEST(test_a_bad_hash_copies_nothing);
    RUN_TEST(test_a_wrong_board_id_copies_nothing);
    RUN_TEST(test_a_missing_mark_copies_nothing);
    RUN_TEST(test_an_image_below_the_floor_copies_nothing);
    RUN_TEST(test_no_new_round_over_a_pending_image);
    RUN_TEST(test_an_installed_image_is_not_copied_again);
    RUN_TEST(test_a_copy_cut_at_a_chosen_page_recovers_on_the_next_boot);
    RUN_TEST(test_an_image_ending_at_the_end_of_stage);
    RUN_TEST(test_a_fallback_clears_the_mark_and_the_next_reset_takes_nothing);
    RUN_TEST(test_an_uncleared_mark_after_a_fallback_is_taken_again);
    RUN_TEST(test_a_clear_on_a_silent_store_fails);
    return UNITY_END();
}
