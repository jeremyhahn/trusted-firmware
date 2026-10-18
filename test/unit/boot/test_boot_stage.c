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
#include "boot_verify_mock.h"
#include "../../../testdata/vectors.h"

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
static uint8_t             good_sig[TFW_BOOT_SIG_LEN];
static const uint8_t       mode0 = 0u;

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
        tfw_nor_model_load(MARK + TFW_STAGE_MARK_O_SIG, good_sig, sizeof good_sig);
        tfw_nor_model_load(MARK + TFW_STAGE_MARK_O_MODE, &mode0, 1u);
    }
}

/* The software NOR part behind faults of the test's choosing: a status that
 * never clears WIP, a JEDEC id an absent part reads (`blank_id` 1: zeros, 2:
 * ones), and a store that goes silent after its `cut_at`th command. `sig_at`
 * is the command that read the MARK's signature. */
static unsigned xfers;
static unsigned cut_at;
static unsigned sig_at;
static uint8_t  busy_forever;
static uint8_t  blank_id;

static void faulty_xfer(uint8_t *buf, uint16_t len)
{
    xfers++;
    if (busy_forever != 0u && buf[0] == 0x05u) {
        buf[1] = 0x01u;
        return;
    }
    if (blank_id != 0u && buf[0] == 0x9Fu) {
        memset(&buf[1], (blank_id == 1u) ? 0x00 : 0xFF, 3u);
        return;
    }
    if (buf[0] == 0x03u && sig_at == 0u &&
        (((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3]) ==
            MARK + TFW_STAGE_MARK_O_SIG) {
        sig_at = xfers;
    }
    tfw_nor_model_xfer(buf, len);
    if (xfers == cut_at) {
        tfw_boot_mock_on_store(NULL);
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
    memset(good_sig, 0x5A, sizeof good_sig);
    tfw_boot_verify_mock_want(NULL);
    tfw_boot_verify_mock_pubkey(TFW_OK);
    xfers = 0u;
    cut_at = 0u;
    sig_at = 0u;
    busy_forever = 0u;
    blank_id = 0u;
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

/* #137: tfw-boot verifies the row key's signature over the activate
 * Sig_structure before it takes the image. */
static void test_an_authorized_image_is_taken_over_the_sig_structure(void)
{
    static const uint8_t vec[] = TFWV_OTA_ACTIVATE_SIG_STRUCTURE;
    uint8_t              want[sizeof vec - 1u];
    const uint8_t       *msg;
    uint16_t             len;

    tfw_boot_verify_mock_want(good_sig);
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, VERSION));
    assert_installed(1200u);
    memcpy(want, vec, sizeof want);
    memcpy(&want[TFWV_OTA_ACTIVATE_SIG_STRUCTURE_HASH_AT], &trailer[TFW_BOOT_TR_MEASUREMENT],
           TFW_BOOT_MEASURE_LEN);
    msg = tfw_boot_verify_mock_msg(&len);
    TEST_ASSERT_EQUAL_UINT16(sizeof want, len);
    TEST_ASSERT_EQUAL_MEMORY(want, msg, sizeof want);
}

static void assert_refused_and_forgotten(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_boot_stage_take(&plat, VERSION));
    assert_nothing_copied();
    TEST_ASSERT_EQUAL_HEX8(0x00u, tfw_nor_model_mem()[MARK]);
    /* The next reset takes nothing. */
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, VERSION));
    assert_nothing_copied();
}

static void test_a_bad_authorization_clears_the_mark(void)
{
    static const uint8_t other[TFW_BOOT_SIG_LEN] = { 1u };

    tfw_boot_verify_mock_want(other);
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    assert_refused_and_forgotten();
}

static void test_a_missing_authorization_clears_the_mark(void)
{
    uint8_t erased[TFW_BOOT_SIG_LEN + 1u];

    tfw_boot_verify_mock_want(good_sig);
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    memset(erased, 0xFF, sizeof erased);
    tfw_nor_model_load(MARK + TFW_STAGE_MARK_O_SIG, erased, sizeof erased);
    assert_refused_and_forgotten();
}

/* A part still mid-write after every status read tfw-boot allows. */
static void test_a_store_busy_for_ever_times_out(void)
{
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    tfw_boot_mock_on_store(faulty_xfer);
    busy_forever = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_boot_stage_take(&plat, 0u));
    assert_nothing_copied();
}

/* No part on the bus: MISO floating low or high reads an id of all zeros or
 * all ones. */
static void test_an_absent_store_is_refused_by_its_id(void)
{
    uint8_t b;

    for (b = 1u; b <= 2u; b++) {
        setUp();
        stage(1000u, 1200u, TFW_BOARD_ID, 1);
        tfw_boot_mock_on_store(faulty_xfer);
        blank_id = b;
        TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_boot_stage_take(&plat, 0u));
        assert_nothing_copied();
    }
}

/* The store going silent at the MARK's read, then at the trailer's: the
 * JEDEC id and one status read come before them. */
static void test_a_store_silent_at_the_mark_or_the_trailer_copies_nothing(void)
{
    unsigned n;

    for (n = 2u; n <= 3u; n++) {
        setUp();
        stage(1000u, 1200u, TFW_BOARD_ID, 1);
        tfw_boot_mock_on_store(faulty_xfer);
        cut_at = n;
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(&plat, 0u));
        assert_nothing_copied();
        TEST_ASSERT_EQUAL_UINT(n, xfers);
    }
}

/* Silent at the signature's read: the error, not AUTH_REQUIRED, so the mark
 * is not forgotten. */
static void test_a_store_silent_at_the_signature_keeps_the_mark(void)
{
    unsigned n_sig;

    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    tfw_boot_mock_on_store(faulty_xfer);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, 0u));
    TEST_ASSERT_NOT_EQUAL_UINT(0u, sig_at);

    n_sig = sig_at;
    setUp();
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    tfw_boot_mock_on_store(faulty_xfer);
    cut_at = n_sig - 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_stage_take(&plat, 0u));
    assert_nothing_copied();
    TEST_ASSERT_EQUAL_HEX8((uint8_t)TFW_STAGE_MARK_MAGIC, tfw_nor_model_mem()[MARK]);
}

/* A MARK whose length leaves no body, or runs past STAGE. */
static void test_a_mark_length_out_of_range_copies_nothing(void)
{
    const uint32_t bad[] = { TFW_BOOT_TRAILER_LEN, (uint32_t)TFW_OTA_STORE_STAGE_LEN + 1u };
    uint8_t        w[4];
    unsigned       i;

    for (i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        setUp();
        stage(1000u, 1200u, TFW_BOARD_ID, 1);
        wr32(w, bad[i]);
        tfw_nor_model_load(MARK + TFW_STAGE_MARK_O_LEN, w, sizeof w);
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_stage_take(&plat, 0u));
        assert_nothing_copied();
    }
}

/* A trailer whose image is longer than the body the MARK names. */
static void test_an_image_longer_than_its_body_copies_nothing(void)
{
    stage(1200u - TFW_BOOT_TRAILER_LEN + 1u, 1200u, TFW_BOARD_ID, 1);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_stage_take(&plat, 0u));
    assert_nothing_copied();
}

/* The version in the Sig_structure as CBOR's shortest unsigned integer, in
 * each of its one-, two- and four-byte forms (RFC 8949 §4.2.1). */
static void test_the_activate_version_is_shortest_cbor(void)
{
    static const uint8_t enc[3][6] = {
        { 0x18u, 0x80u, 0x00u },
        { 0x19u, 0x12u, 0x34u, 0x00u },
        { 0x1Au, 0x12u, 0x34u, 0x56u, 0x78u, 0x00u },
    };
    static const uint32_t ver[3] = { 0x80u, 0x1234u, 0x12345678uL };
    static const uint16_t n[3]   = { 3u, 4u, 6u };
    const uint8_t        *msg;
    uint16_t              len;
    uint8_t               w[4];
    unsigned              i;

    for (i = 0u; i < 3u; i++) {
        setUp();
        stage(1000u, 1200u, TFW_BOARD_ID, 1);
        wr32(w, ver[i]);
        tfw_nor_model_load(STAGE + 1200u - TFW_BOOT_TRAILER_LEN + TFW_BOOT_TR_IMAGE_VERSION, w,
                           sizeof w);
        memcpy(&trailer[TFW_BOOT_TR_IMAGE_VERSION], w, sizeof w);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, ver[i]));
        assert_installed(1200u);
        msg = tfw_boot_verify_mock_msg(&len);
        TEST_ASSERT_TRUE(len > n[i]);
        TEST_ASSERT_EQUAL_MEMORY(enc[i], &msg[len - n[i]], n[i]);
    }
}

/* A tier whose element keeps its key: the verify is given none. */
static void test_a_keyless_tier_verifies_without_a_key(void)
{
    tfw_boot_verify_mock_pubkey(TFW_ERR_UNSUPPORTED);
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, VERSION));
    assert_installed(1200u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_verify_mock_keyless());
}

/* A key that cannot be read: its error, and the mark kept. */
static void test_an_unreadable_key_copies_nothing_and_keeps_the_mark(void)
{
    tfw_boot_verify_mock_pubkey(TFW_ERR_NACK);
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_boot_stage_take(&plat, VERSION));
    assert_nothing_copied();
    TEST_ASSERT_EQUAL_HEX8((uint8_t)TFW_STAGE_MARK_MAGIC, tfw_nor_model_mem()[MARK]);
}

/* A part reporting no page, or one larger than the copy's buffer: nothing
 * written, and a take cut there stays STAGED for the next boot. */
static void test_a_page_size_the_copy_cannot_use_is_refused(void)
{
    stage(1000u, 1200u, TFW_BOARD_ID, 1);
    tfw_boot_mock_set_page_size(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_stage_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_STAGED, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());

    tfw_boot_mock_set_page_size((uint16_t)(TFW_OTA_PAGE_MAX + 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_stage_copy(STAGE, 1200u - TFW_BOOT_TRAILER_LEN));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_valid_image_copies_and_enters_its_trial);
    RUN_TEST(test_a_bad_hash_copies_nothing);
    RUN_TEST(test_a_wrong_board_id_copies_nothing);
    RUN_TEST(test_a_missing_mark_copies_nothing);
    RUN_TEST(test_an_authorized_image_is_taken_over_the_sig_structure);
    RUN_TEST(test_a_bad_authorization_clears_the_mark);
    RUN_TEST(test_a_missing_authorization_clears_the_mark);
    RUN_TEST(test_an_image_below_the_floor_copies_nothing);
    RUN_TEST(test_no_new_round_over_a_pending_image);
    RUN_TEST(test_an_installed_image_is_not_copied_again);
    RUN_TEST(test_a_copy_cut_at_a_chosen_page_recovers_on_the_next_boot);
    RUN_TEST(test_an_image_ending_at_the_end_of_stage);
    RUN_TEST(test_a_fallback_clears_the_mark_and_the_next_reset_takes_nothing);
    RUN_TEST(test_an_uncleared_mark_after_a_fallback_is_taken_again);
    RUN_TEST(test_a_clear_on_a_silent_store_fails);
    RUN_TEST(test_a_store_busy_for_ever_times_out);
    RUN_TEST(test_an_absent_store_is_refused_by_its_id);
    RUN_TEST(test_a_store_silent_at_the_mark_or_the_trailer_copies_nothing);
    RUN_TEST(test_a_store_silent_at_the_signature_keeps_the_mark);
    RUN_TEST(test_a_mark_length_out_of_range_copies_nothing);
    RUN_TEST(test_an_image_longer_than_its_body_copies_nothing);
    RUN_TEST(test_the_activate_version_is_shortest_cbor);
    RUN_TEST(test_a_keyless_tier_verifies_without_a_key);
    RUN_TEST(test_an_unreadable_key_copies_nothing_and_keeps_the_mark);
    RUN_TEST(test_a_page_size_the_copy_cannot_use_is_refused);
    return UNITY_END();
}
