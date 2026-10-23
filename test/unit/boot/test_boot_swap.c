/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The 2560's swap-move (issue #75), built under the 2560's profile only: a
 * full swap, a reset cut at every page write of both phases, a rollback, and
 * images that fill the slot to its last byte.
 */
#include <limits.h>
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "boot_measure.h"
#include "boot_policy.h"
#include "boot_recovery.h"
#include "boot_stage.h"
#include "boot_store.h"
#include "boot_swap.h"
#include "boot_trial.h"
#include "crc16.h"
#include "frame.h"
#include "../../model/spi_nor_model.h"

#define SLOT  ((uint32_t)TFW_SWAP_SLOT_LEN)
#define PRI   ((uint32_t)TFW_SWAP_PRIMARY_BASE)
#define SEC   ((uint32_t)TFW_SWAP_SECONDARY_BASE)
#define REC   ((uint16_t)TFW_SWAP_RECORD_ADDR)
/* Every page write a whole swap makes: the move, then a and b per unit. */
#define WRITES ((unsigned)(3u * (SLOT / tfw_boot_flash_page_size())))

static uint8_t img_a[TFW_SWAP_SLOT_LEN];
static uint8_t img_b[TFW_SWAP_SLOT_LEN];
static unsigned stores_left = UINT_MAX;   /* platform stores until the power goes */
static uint32_t refuse_at   = 0xFFFFFFFFu;   /* the slot base the gate refuses */

/* Two images that differ in every byte, with no period a shifted copy could
 * match by accident. */
static void seed(void)
{
    /* The two images computed once and copied in at every setUp: the torn-
     * write cases call setUp once per write, byte and value (#163). A case
     * that changes img_a or img_b gets them back from these at the next. */
    static uint8_t pat_a[SLOT];
    static uint8_t pat_b[SLOT];
    static uint8_t made;
    uint32_t       i;

    if (made == 0u) {
        for (i = 0u; i < SLOT; i++) {
            pat_a[i] = (uint8_t)((i * 7u) ^ (i >> 8));
            pat_b[i] = (uint8_t)~pat_a[i];
        }
        made = 1u;
    }
    memcpy(img_a, pat_a, SLOT);
    memcpy(img_b, pat_b, SLOT);
    memcpy(&tfw_boot_mock_flash()[PRI], img_a, SLOT);
    memcpy(&tfw_boot_mock_flash()[SEC], img_b, SLOT);
}

static void assert_slots(const uint8_t *pri, const uint8_t *sec)
{
    TEST_ASSERT_EQUAL_MEMORY(pri, &tfw_boot_mock_flash()[PRI], SLOT);
    TEST_ASSERT_EQUAL_MEMORY(sec, &tfw_boot_mock_flash()[SEC], SLOT);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_swap_in_progress());
}

void setUp(void)
{
    tfw_boot_mock_reset();
    seed();
    stores_left = UINT_MAX;
    refuse_at   = 0xFFFFFFFFu;
}

void tearDown(void) {}

static void test_a_full_swap_exchanges_the_slots(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
    assert_slots(img_b, img_a);
    TEST_ASSERT_EQUAL_UINT(WRITES, tfw_boot_mock_page_writes());
}

/* A part reporting no page, one larger than a unit, or one that does not
 * divide a unit: the swap moves nothing. */
static void test_a_page_size_the_swap_cannot_use_moves_nothing(void)
{
    const uint16_t bad[] = { 0u, (uint16_t)(TFW_OTA_PAGE_MAX + 1u),
                             (uint16_t)(TFW_OTA_PAGE_MAX / 2u + 1u) };
    unsigned       i;

    for (i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        tfw_boot_mock_set_page_size(bad[i]);
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_swap());
        assert_slots(img_a, img_b);
        TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
    }
}

static void test_a_rollback_restores_a(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
    assert_slots(img_a, img_b);
}

/* A cut after k page writes, for every k a swap makes: the move phase, the
 * swap's a and b on even and odd steps, and the torn half of a unit. The next
 * boot sees the record, resumes, and ends with the slots exchanged. */
static void test_a_cut_at_any_page_resumes(void)
{
    unsigned k;
    unsigned all = WRITES;

    for (k = 0u; k < all; k++) {
        setUp();
        tfw_boot_mock_cut_after(k);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_swap());
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_swap_in_progress());
        tfw_boot_mock_cut_after(UINT_MAX);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
        assert_slots(img_b, img_a);
    }
}

/* A cut in the rollback's own swap resumes into the rollback, not forward. */
static void test_a_cut_in_the_rollback_still_restores_a(void)
{
    static const unsigned at[] = { 1u, 513u, 514u, 1535u };
    unsigned n;

    for (n = 0u; n < sizeof at / sizeof at[0]; n++) {
        setUp();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
        tfw_boot_mock_cut_after(at[n]);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_swap());
        tfw_boot_mock_cut_after(UINT_MAX);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
        assert_slots(img_a, img_b);
    }
}

/* The images above fill the slot; this pins its ends: the trailer's last byte
 * at the slot's top, and the byte after the slot left alone in both. */
static void test_an_image_filling_the_slot_moves_its_last_byte(void)
{
    uint8_t *f = tfw_boot_mock_flash();

    f[PRI + SLOT - 1u] = 0xA5u;
    f[SEC + SLOT - 1u] = 0x5Au;
    img_a[SLOT - 1u]   = 0xA5u;
    img_b[SLOT - 1u]   = 0x5Au;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
    assert_slots(img_b, img_a);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, f[SEC + SLOT]);
}

/* The phase in the journal's newest valid copy, read as boot_swap.c lays it
 * out: two copies of {phase, step, sequence, CRC-16 of the three}. Idle with
 * neither valid. */
#define COPY 5u

static uint8_t copy_ok(const uint8_t *c)
{
    return (uint8_t)(tfw_crc16(c, 3u) == (uint16_t)(c[3] | (c[4] << 8)));
}

static uint8_t phase(void)
{
    const uint8_t *c = &tfw_boot_mock_nvm()[REC];

    if (copy_ok(c + COPY) && (!copy_ok(c) || (int8_t)(c[COPY + 2u] - c[2]) > 0)) {
        return c[COPY];
    }
    return copy_ok(c) ? c[0] : 0xFFu;
}

static void test_the_record_reads_swapped_then_idle(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
    TEST_ASSERT_EQUAL_HEX8(0x44u, phase());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, phase());
}

/* ---- The boot's own sequence: main()'s three calls, around #82's trial ---- */

static tfw_boot_platform_t plat;
static tfw_boot_platform_t stored;
static unsigned            stores;

void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    if (stores_left == 0u) {
        return;   /* the power went with the flash cut */
    }
    stores_left--;
    stored = *p;
    stores++;
}

/* The stage has copied B into the secondary and left it PENDING. */
static void pending(void)
{
    memset(&plat, 0, sizeof plat);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_PENDING;
    stores          = 0u;
}

/* A reset: the block as last stored, and main()'s first call. */
static tfw_boot_reset_action_t reset(void)
{
    plat = stored;
    tfw_boot_mock_cut_after(UINT_MAX);
    return tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat));
}

/* A reset in each phase of the forward swap: the next boot finishes it before
 * the gate, which then evaluates B in the primary. */
static void test_a_boot_resumes_a_swap_cut_in_each_phase(void)
{
    static const unsigned at[] = { 0u, 3u, 511u, 512u, 513u, 514u, 515u, 517u, 1535u };
    unsigned n;

    for (n = 0u; n < sizeof at / sizeof at[0]; n++) {
        setUp();
        pending();
        stored = plat;
        tfw_boot_mock_cut_after(at[n]);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
        TEST_ASSERT_EQUAL(TFW_BOOT_RESET_TRY, reset());
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));   /* swapped: nothing */
        assert_slots(img_b, img_a);
    }
}

static void test_a_confirmed_trial_keeps_the_swap(void)
{
    pending();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_confirm(&plat));
    tfw_boot_platform_store(&plat);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, reset());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, phase());
    assert_slots(img_b, img_a);
    plat.boot_state = TFW_BOOT_STATE_GOOD;   /* main()'s settle */
    tfw_boot_platform_store(&plat);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());   /* and stays */
    assert_slots(img_b, img_a);
}

static void test_a_failed_trial_swaps_back_and_boots_a(void)
{
    pending();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    tfw_boot_platform_store(&plat);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
    assert_slots(img_a, img_b);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());   /* and stays */
    assert_slots(img_a, img_b);
}

/* A reset in the swap back, or before it began: the next boot finishes it. */
static void test_a_fall_back_cut_short_is_finished_at_the_next_boot(void)
{
    static const unsigned at[] = { 0u, 7u, 512u, 1000u, 1535u };
    unsigned n;

    for (n = 0u; n < sizeof at / sizeof at[0]; n++) {
        setUp();
        pending();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
        tfw_boot_platform_store(&plat);
        plat = stored;
        tfw_boot_mock_cut_after(at[n]);
        (void)tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat));
        TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
        TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
        assert_slots(img_a, img_b);
    }
}

/* B refused by the gate after the swap: back, before any trial. */
static void test_an_image_refused_after_the_swap_swaps_back(void)
{
    pending();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_fall_back(&plat));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, plat.ota_state);
    TEST_ASSERT_EQUAL_UINT(1u, stores);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&plat));
    assert_slots(img_a, img_b);
}

/* Nothing swapped, nothing to fall back to: an ordinary failed trial. */
static void test_no_fall_back_without_a_swap(void)
{
    pending();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_swap_fall_back(&plat));
    TEST_ASSERT_EQUAL_UINT(0u, stores);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
}

/* The gate is the test's: it refuses the image at `refuse_at`, and notes the
 * page writes done when it checked the secondary and whether the primary held
 * the image sent when it checked there. */
#define SENT  256u
#define PAGES ((unsigned)(SENT / tfw_boot_flash_page_size()))

static uint8_t  img_n[TFW_SWAP_SLOT_LEN];
static unsigned evaluates;
static unsigned evaluated_sent;
static unsigned writes_at_check;
static unsigned hand_overs;

tfw_status_t tfw_boot_evaluate_at(uint32_t base, uint32_t floor, tfw_boot_verdict_t *out)
{
    (void)floor;
    memset(out, 0, sizeof *out);
    evaluates++;
    if (base == SEC) {
        writes_at_check = tfw_boot_mock_page_writes();
    } else {
        evaluated_sent += (unsigned)(memcmp(&tfw_boot_mock_flash()[PRI], img_n, SLOT) == 0);
    }
    return (base == refuse_at) ? TFW_ERR_CRYPTO : TFW_OK;
}

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    return tfw_boot_evaluate_at(TFW_BOOT_APP_BASE, floor, out);
}

void tfw_boot_hand_over(const tfw_boot_verdict_t *v)
{
    (void)v;
    hand_overs++;
}

tfw_status_t tfw_boot_floor_load(const tfw_boot_platform_t *p, uint32_t *out)
{
    *out = p->rollback_floor;
    return TFW_OK;
}

static void test_an_image_refused_in_the_secondary_is_never_swapped(void)
{
    pending();
    refuse_at = SEC;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
    assert_slots(img_a, img_b);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());   /* and not taken again */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
}

/* The serial session (issue #75): its image goes to the secondary, is checked
 * there and swapped in before the gate. */
static void feed(uint8_t seq, uint8_t cmd, const uint8_t *p, uint8_t n)
{
    uint8_t  buf[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(buf, sizeof buf, seq, cmd, p, n);

    TEST_ASSERT_TRUE(len > 0u);
    tfw_boot_mock_feed_rx(buf, len);
}

/* A requested session from GOOD that sends SENT bytes of 0xA5, and OTA_END
 * if `end`: the sent image is those bytes over the rest of B. */
static void session(uint8_t end)
{
    uint8_t  b[4] = { (uint8_t)SENT, (uint8_t)(SENT >> 8), 0u, 0u };
    uint8_t  d[4u + 32u];
    uint32_t off;
    tfw_boot_session_limits_t lim;

    memset(img_n, 0xA5, SENT);
    memcpy(&img_n[SENT], &img_b[SENT], SLOT - SENT);
    evaluates       = 0u;
    evaluated_sent  = 0u;
    writes_at_check = 0u;
    hand_overs      = 0u;
    stores          = 0u;
    memset(&plat, 0, sizeof plat);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_IDLE;
    stored          = plat;

    feed(1u, TFW_CMD_OTA_BEGIN, b, 4u);
    memset(&d[4], 0xA5, 32u);
    for (off = 0u; off < SENT; off += 32u) {
        d[0] = (uint8_t)off; d[1] = (uint8_t)(off >> 8); d[2] = 0u; d[3] = 0u;
        feed(2u, TFW_CMD_OTA_DATA, d, sizeof d);
    }
    if (end != 0u) {
        feed(3u, TFW_CMD_OTA_END, 0, 0u);
    }
    lim.idle_polls = 100u;
    lim.idle_bytes = 0u;
    lim.max_polls  = 0u;
    lim.max_bytes  = 0u;
    tfw_boot_session_open(&plat, &lim);
}

static void test_a_session_cut_short_leaves_the_primary_to_boot(void)
{
    session(0u);
    TEST_ASSERT_EQUAL_UINT(0u, evaluates);
    TEST_ASSERT_EQUAL_UINT(0u, stores);   /* no STAGED: the primary is whole */
    assert_slots(img_a, img_n);
    /* The reset: an ordinary boot, and nothing in the secondary is taken. */
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(PAGES, tfw_boot_mock_page_writes());
    assert_slots(img_a, img_n);
}

static void test_a_session_image_is_checked_then_swapped_then_tried(void)
{
    session(1u);
    TEST_ASSERT_EQUAL_UINT(2u, evaluates);   /* the secondary, then the primary */
    TEST_ASSERT_EQUAL_UINT(PAGES, writes_at_check);
    TEST_ASSERT_EQUAL_UINT(1u, evaluated_sent);
    TEST_ASSERT_EQUAL_UINT(PAGES + WRITES, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT(1u, hand_overs);
    assert_slots(img_n, img_a);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_IDLE, stored.ota_state);
    /* Never confirmed: the next boot swaps back to A, not to recovery. */
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    assert_slots(img_a, img_n);
}

static void test_a_session_image_refused_in_the_secondary_writes_no_swap_page(void)
{
    refuse_at = SEC;
    session(1u);
    TEST_ASSERT_EQUAL_UINT(1u, evaluates);
    TEST_ASSERT_EQUAL_UINT(PAGES, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
    assert_slots(img_a, img_n);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(PAGES, tfw_boot_mock_page_writes());
    assert_slots(img_a, img_n);
}

static void test_a_boot_after_a_session_cut_in_its_swap_enters_the_trial(void)
{
    stores_left = 1u;   /* PENDING at OTA_END, then the power goes */
    tfw_boot_mock_cut_after(PAGES + WRITES / 2u);
    session(1u);
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_swap_in_progress());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_PENDING, stored.ota_state);

    stores_left = UINT_MAX;
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_TRY, reset());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));   /* swapped: nothing */
    assert_slots(img_n, img_a);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, plat.boot_state);
}

/* A reset inside a progress write, for every write of a swap each way, at each
 * of the copy's bytes, left erased or cleared: the copy written is torn, the
 * other still names the step before, and the next boot resumes from it. */
static void tear_every_write(uint8_t back)
{
    static const uint8_t vals[] = { 0xFFu, 0x00u };
    unsigned w;
    uint8_t  b;
    uint8_t  v;
    uint8_t  more = 1u;

    for (w = 0u; more != 0u; w++) {
        for (b = 0u; b < COPY; b++) {
            for (v = 0u; v < sizeof vals; v++) {
                setUp();
                if (back != 0u) {
                    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
                }
                tfw_boot_mock_tear_nvm(w, b, vals[v]);
                if (tfw_boot_swap() == TFW_OK) {
                    more = 0u;   /* w is past the last write: every one torn */
                    break;
                }
                tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
                TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
                if (back != 0u) {
                    assert_slots(img_a, img_b);
                } else {
                    assert_slots(img_b, img_a);
                }
            }
        }
    }
    TEST_ASSERT_TRUE(w > WRITES / 2u);   /* a progress write per unit and more */
}

static void test_a_torn_progress_write_resumes_the_swap(void)
{
    tear_every_write(0u);
}

static void test_a_torn_progress_write_resumes_the_swap_back(void)
{
    tear_every_write(1u);
}

/* The settle's idle write torn: still swapped, and the next boot settles. */
static void test_a_torn_settle_settles_at_the_next_boot(void)
{
    pending();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_confirm(&plat));
    tfw_boot_platform_store(&plat);
    tfw_boot_mock_tear_nvm(0u, 1u, 0x00u);
    plat = stored;
    (void)tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat));
    TEST_ASSERT_EQUAL_HEX8(0x44u, phase());
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, reset());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, phase());
    assert_slots(img_b, img_a);
}

/* Codex review 5268 P2: an idle write that fails its readback settles nothing.
 * main() advances the floor and stores GOOD only on SETTLE, so the block stays
 * CONFIRMED at the old floor, and the next reset settles it -- PH_DONE with
 * GOOD, which would swap back below the new floor, is never stored. */
static void test_a_settle_whose_idle_write_fails_settles_nothing(void)
{
    tfw_boot_reset_action_t act;

    pending();
    plat.rollback_floor = 3u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 3u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_confirm(&plat));
    tfw_boot_platform_store(&plat);
    tfw_boot_mock_tear_nvm(0u, 1u, 0x00u);
    plat = stored;
    act = tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat));
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, act);   /* not SETTLE: no advance */
    TEST_ASSERT_EQUAL_HEX8(0x44u, phase());
    TEST_ASSERT_EQUAL(TFW_BOOT_STATE_CONFIRMED, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT32(3u, stored.rollback_floor);
    assert_slots(img_b, img_a);

    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, reset());   /* retried */
    TEST_ASSERT_EQUAL_HEX8(0xFFu, phase());
    assert_slots(img_b, img_a);
    plat.rollback_floor = 5u;                 /* main()'s settle */
    plat.boot_state     = TFW_BOOT_STATE_GOOD;
    tfw_boot_platform_store(&plat);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());   /* and no swap back */
    assert_slots(img_b, img_a);
}

/* Both copies damaged, here in the middle of a swap: no position can be read,
 * so nothing is moved on a guess. The record reads idle, the boot leaves the
 * slots as they are for the gate to judge, and a later swap starts whole. */
static void test_garbage_in_both_copies_moves_nothing(void)
{
    unsigned before;

    pending();
    stored = plat;
    tfw_boot_mock_cut_after(WRITES / 2u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    tfw_boot_mock_cut_after(UINT_MAX);
    memset(&tfw_boot_mock_nvm()[REC], 0x5A, 2u * COPY);
    before = tfw_boot_mock_page_writes();

    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_swap_in_progress());
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_TRY, reset());
    TEST_ASSERT_EQUAL_UINT(before, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_swap_fall_back(&plat));
    TEST_ASSERT_EQUAL_UINT(before, tfw_boot_mock_page_writes());

    setUp();
    memset(&tfw_boot_mock_nvm()[REC], 0x5A, 2u * COPY);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap());
    assert_slots(img_b, img_a);
}

/* ---- The store row (BOOT_STORE=1): the copy, the swap and the trial ----
 *
 * main()'s order on the store row: tfw_boot_stage_take copies the staged image
 * into the secondary and leaves it PENDING, tfw_boot_swap_take swaps it in,
 * and it is armed for its one trial. The tests above start from that PENDING;
 * these reach it through the real copy, from the software NOR part. */

#define ST_VERSION 7u

static uint8_t st_stream[TFW_OTA_STORE_STAGE_LEN];
static uint8_t img_s[TFW_SWAP_SLOT_LEN];   /* the secondary after the copy */

static const uint8_t *st_src;

static tfw_status_t st_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    memcpy(buf, &st_src[addr], len);
    return TFW_OK;
}

static void st_wr32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

/* What the application's stager leaves in the store, as test_boot_stage.c
 * lays it out: a 1000-byte image padded to 1072, its trailer, and the mark. */
static void st_stage(void)
{
    uint32_t total = 1200u;
    uint32_t body  = total - TFW_BOOT_TRAILER_LEN;
    uint8_t  digest[TFW_BOOT_MEASURE_LEN];
    uint8_t  mark[TFW_STAGE_MARK_LEN];
    uint8_t *tr    = &st_stream[body];
    uint32_t i;

    tfw_nor_model_reset();
    tfw_boot_mock_on_store(tfw_nor_model_xfer);
    for (i = 0u; i < body; i++) {
        st_stream[i] = (uint8_t)(i * 5u + 1u);
    }
    st_src = st_stream;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(st_read, 0u, 1000u, digest));
    memset(tr, 0, TFW_BOOT_TRAILER_LEN);
    st_wr32(&tr[TFW_BOOT_TR_MAGIC], TFW_BOOT_TRAILER_MAGIC);
    tr[TFW_BOOT_TR_VERSION]  = (uint8_t)TFW_BOOT_TRAILER_VERSION;
    tr[TFW_BOOT_TR_BOARD_ID] = TFW_BOARD_ID;
    st_wr32(&tr[TFW_BOOT_TR_IMAGE_VERSION], ST_VERSION);
    st_wr32(&tr[TFW_BOOT_TR_IMAGE_LEN], 1000u);
    memcpy(&tr[TFW_BOOT_TR_MEASUREMENT], digest, sizeof digest);
    memset(&tr[TFW_BOOT_TR_SIGNATURE], 0x5A, TFW_BOOT_SIG_LEN);
    tfw_nor_model_load((uint32_t)TFW_OTA_STORE_STAGE_ADDR, st_stream, total);
    st_wr32(mark, TFW_STAGE_MARK_MAGIC);
    st_wr32(&mark[TFW_STAGE_MARK_O_LEN], total);
    memcpy(&mark[TFW_STAGE_MARK_O_HASH], digest, sizeof digest);
    tfw_nor_model_load((uint32_t)TFW_OTA_STORE_MARK_ADDR, mark, sizeof mark);

    memset(&plat, 0, sizeof plat);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_IDLE;
    stored = plat;
    stores = 0u;
}

/* One boot of the store row up to the jump: copy, swap, arm. */
static void st_boot_into_trial(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_stage_take(&plat, ST_VERSION));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_PENDING, stored.ota_state);
    assert_slots(img_a, &tfw_boot_mock_flash()[SEC]);
    memcpy(img_s, &tfw_boot_mock_flash()[SEC], SLOT);
    TEST_ASSERT_EQUAL_MEMORY(st_stream, img_s, 1000u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, ST_VERSION));
    assert_slots(img_s, img_a);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    tfw_boot_platform_store(&plat);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_nor_model_violations());
}

/* (a) and (c): a store image runs only on trial, and a reset that finds the
 * trial unconfirmed swaps back to A -- and the mark the store still holds does
 * not give the image that failed a second trial. */
static void test_a_store_image_unconfirmed_swaps_back_and_is_not_retried(void)
{
    unsigned writes;

    st_stage();
    st_boot_into_trial();
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
    assert_slots(img_a, img_s);

    writes = tfw_boot_mock_page_writes();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_stage_take(&plat, ST_VERSION));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, ST_VERSION));   /* nothing pending */
    TEST_ASSERT_EQUAL_UINT(writes, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    assert_slots(img_a, img_s);
}

/* (b): only the application's confirm keeps it. Confirmed, the next reset
 * settles with the slots kept, and the store's mark copies nothing again. */
static void test_a_confirmed_store_image_settles_and_is_not_copied_again(void)
{
    unsigned writes;

    st_stage();
    st_boot_into_trial();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_confirm(&plat));
    tfw_boot_platform_store(&plat);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, reset());
    assert_slots(img_s, img_a);
    plat.boot_state = TFW_BOOT_STATE_GOOD;   /* main()'s settle */
    tfw_boot_platform_store(&plat);

    writes = tfw_boot_mock_page_writes();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_stage_take(&plat, ST_VERSION));
    TEST_ASSERT_EQUAL_UINT(writes, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    assert_slots(img_s, img_a);
}

/* (d), the copy's half: below the floor, the store image is neither copied
 * nor swapped, and A boots as it was. */
static void test_a_store_image_below_the_floor_never_reaches_a_slot(void)
{
    st_stage();
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION,
                      tfw_boot_stage_take(&plat, ST_VERSION + 1u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, ST_VERSION + 1u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT(0u, stores);
    assert_slots(img_a, img_b);
}

/* A failed trial whose fall back stores the block and then has the first
 * journal write of the swap back torn: the block is GOOD and FAILED, the
 * record still swapped. The next boot is an ordinary one, and at_reset's
 * BOOT case runs the whole swap back from there. */
static void test_a_fall_back_torn_before_the_swap_back_runs_it_at_the_next_boot(void)
{
    tfw_boot_reset_action_t act;

    pending();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    tfw_boot_platform_store(&plat);
    plat = stored;
    tfw_boot_mock_tear_nvm(0u, 0u, 0x00u);
    act = tfw_boot_swap_at_reset(&plat, tfw_boot_trial_at_reset(&plat));
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, act);
    TEST_ASSERT_EQUAL_HEX8(0x44u, phase());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
    assert_slots(img_b, img_a);

    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, phase());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
    assert_slots(img_a, img_b);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());   /* and stays */
    assert_slots(img_a, img_b);
}

/* Swapped, with a block the trial cannot fail -- already fallen back: the
 * fall back is refused before it stores the block or moves a page. */
static void test_a_fall_back_over_a_block_already_failed_moves_nothing(void)
{
    unsigned pages;

    pending();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_FAILED;
    stores = 0u;
    pages  = tfw_boot_mock_page_writes();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_swap_fall_back(&plat));
    TEST_ASSERT_EQUAL_UINT(0u, stores);
    TEST_ASSERT_EQUAL_UINT(pages, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_HEX8(0x44u, phase());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, plat.ota_state);
    assert_slots(img_b, img_a);
}

/* A commit while swapped is refused untouched: the primary holds the image on
 * trial, and the gate would evaluate that instead of the one just sent. */
static void test_a_commit_while_swapped_is_refused_untouched(void)
{
    tfw_boot_platform_t before;
    unsigned            pages;

    pending();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    stores = 0u;
    pages  = tfw_boot_mock_page_writes();
    before = plat;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_swap_commit(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, stores);
    TEST_ASSERT_EQUAL_UINT(pages, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_MEMORY(&before, &plat, sizeof plat);
    TEST_ASSERT_EQUAL_HEX8(0x44u, phase());
    assert_slots(img_b, img_a);
}

/* A journal write the EEPROM reports done but never takes: the read back sees
 * it, and no page is moved under a record that does not say so. The next
 * attempt, over a cell that takes writes again, swaps as usual. */
static void test_a_journal_write_that_does_not_land_moves_nothing(void)
{
    pending();
    tfw_boot_mock_lose_nvm_writes(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, phase());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_PENDING, plat.ota_state);
    assert_slots(img_a, img_b);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x44u, phase());
    assert_slots(img_b, img_a);
}

/* ---- OTA_CONFIRM's request (issue #77, include/tfw/boot.h) ---------------- */

static uint8_t *conf(void) { return tfw_boot_mock_nvm() + TFW_BOOT_CONFIRM_ADDR; }

/* B swapped in and on trial, as the reset after its hand-over finds it. */
static void on_trial(void)
{
    pending();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_swap_take(&plat, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
    tfw_boot_platform_store(&plat);
}

/* The record the application writes, naming the primary's image -- or, with
 * `flip`, another image's measurement. */
static void build_request(uint8_t c[TFW_BOOT_CONFIRM_LEN], uint8_t magic, uint8_t flip)
{
    uint16_t crc;

    c[0] = magic;
    tfw_boot_flash_read(TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_MEASUREMENT,
                        &c[TFW_BOOT_CONFIRM_O_MEAS], TFW_BOOT_CONFIRM_MEAS_LEN);
    c[TFW_BOOT_CONFIRM_O_MEAS] ^= flip;
    crc = tfw_crc16(c, TFW_BOOT_CONFIRM_O_CRC);
    c[TFW_BOOT_CONFIRM_O_CRC]      = (uint8_t)crc;
    c[TFW_BOOT_CONFIRM_O_CRC + 1u] = (uint8_t)(crc >> 8);
}

static void request(uint8_t magic, uint8_t flip)
{
    uint8_t c[TFW_BOOT_CONFIRM_LEN];

    build_request(c, magic, flip);
    memcpy(conf(), c, sizeof c);
}

/* Not confirmed: the trial fails and the next boot is A's again, the record
 * cleared, exactly as a trial with no request. */
static void expect_failed_trial(void)
{
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, reset());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, stored.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, conf()[0]);
    assert_slots(img_a, img_b);
}

static void test_a_valid_confirm_request_settles_the_trial(void)
{
    uint32_t next;

    on_trial();
    request(TFW_BOOT_CONFIRM_MAGIC, 0u);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, reset());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_CONFIRMED, stored.boot_state);  /* before the clear */
    TEST_ASSERT_EQUAL_HEX8(0xFFu, conf()[0]);
    assert_slots(img_b, img_a);
    /* main()'s settle: GOOD, and the floor at the confirmed version. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_settle(&plat, 4u, 0u, &next));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT32(4u, next);
}

static void test_a_stale_confirm_request_fails_the_trial(void)
{
    on_trial();
    request(TFW_BOOT_CONFIRM_MAGIC, 0x01u);   /* another image's measurement */
    expect_failed_trial();
}

static void test_a_corrupt_confirm_request_fails_the_trial(void)
{
    on_trial();
    request(TFW_BOOT_CONFIRM_MAGIC, 0u);
    conf()[TFW_BOOT_CONFIRM_O_CRC] ^= 0x01u;
    expect_failed_trial();

    on_trial();
    request((uint8_t)(TFW_BOOT_CONFIRM_MAGIC ^ 0x01u), 0u);
    expect_failed_trial();
}

static void test_a_trial_with_no_confirm_request_fails(void)
{
    on_trial();
    expect_failed_trial();
}

/* A reset inside the application's record write leaves a CRC that does not
 * match: no request, and the trial fails as an unconfirmed one does. */
static void test_a_torn_confirm_request_confirms_nothing(void)
{
    uint8_t c[TFW_BOOT_CONFIRM_LEN];

    on_trial();
    build_request(c, TFW_BOOT_CONFIRM_MAGIC, 0u);
    tfw_boot_mock_tear_nvm(0u, 5u, 0x00u);
    (void)tfw_boot_nvm_write(TFW_BOOT_CONFIRM_ADDR, c, sizeof c);
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    TEST_ASSERT_EQUAL_HEX8(TFW_BOOT_CONFIRM_MAGIC, conf()[0]);   /* begun, not whole */
    expect_failed_trial();
}

/* main()'s reset as boot_main.c runs it: the trial's verdict, the swap's, and
 * the MARK decided on the first. Returns whether the MARK's magic survived. */
static int st_reset_keeps_mark(tfw_boot_reset_action_t want)
{
    tfw_boot_reset_action_t trial, act;
    const uint8_t          *m;

    plat = stored;
    tfw_boot_mock_cut_after(UINT_MAX);
    trial = tfw_boot_trial_at_reset(&plat);
    act   = tfw_boot_swap_at_reset(&plat, trial);
    TEST_ASSERT_EQUAL(want, act);
    tfw_boot_stage_after_reset(trial, act);
    m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] != 0u || m[1] != 0u || m[2] != 0u || m[3] != 0u;
}

/* Item 56 on the store row: the trial failed and the fallback boots A, and the
 * MARK of the image that failed is still forgotten. */
static void test_a_failed_trial_that_falls_back_forgets_its_mark(void)
{
    st_stage();
    st_boot_into_trial();
    TEST_ASSERT_FALSE(st_reset_keeps_mark(TFW_BOOT_RESET_BOOT));
    assert_slots(img_a, img_s);
}

/* A trial OTA_CONFIRM's request settles keeps the MARK: its image is installed. */
static void test_a_confirmed_store_trial_keeps_its_mark(void)
{
    st_stage();
    st_boot_into_trial();
    request(TFW_BOOT_CONFIRM_MAGIC, 0u);
    TEST_ASSERT_TRUE(st_reset_keeps_mark(TFW_BOOT_RESET_SETTLE));
    assert_slots(img_s, img_a);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_full_swap_exchanges_the_slots);
    RUN_TEST(test_a_page_size_the_swap_cannot_use_moves_nothing);
    RUN_TEST(test_a_rollback_restores_a);
    RUN_TEST(test_a_cut_at_any_page_resumes);
    RUN_TEST(test_a_cut_in_the_rollback_still_restores_a);
    RUN_TEST(test_an_image_filling_the_slot_moves_its_last_byte);
    RUN_TEST(test_the_record_reads_swapped_then_idle);
    RUN_TEST(test_a_boot_resumes_a_swap_cut_in_each_phase);
    RUN_TEST(test_a_confirmed_trial_keeps_the_swap);
    RUN_TEST(test_a_failed_trial_swaps_back_and_boots_a);
    RUN_TEST(test_a_fall_back_cut_short_is_finished_at_the_next_boot);
    RUN_TEST(test_an_image_refused_after_the_swap_swaps_back);
    RUN_TEST(test_no_fall_back_without_a_swap);
    RUN_TEST(test_an_image_refused_in_the_secondary_is_never_swapped);
    RUN_TEST(test_a_session_cut_short_leaves_the_primary_to_boot);
    RUN_TEST(test_a_session_image_is_checked_then_swapped_then_tried);
    RUN_TEST(test_a_session_image_refused_in_the_secondary_writes_no_swap_page);
    RUN_TEST(test_a_boot_after_a_session_cut_in_its_swap_enters_the_trial);
    RUN_TEST(test_a_torn_progress_write_resumes_the_swap);
    RUN_TEST(test_a_torn_progress_write_resumes_the_swap_back);
    RUN_TEST(test_a_torn_settle_settles_at_the_next_boot);
    RUN_TEST(test_a_settle_whose_idle_write_fails_settles_nothing);
    RUN_TEST(test_garbage_in_both_copies_moves_nothing);
    RUN_TEST(test_a_store_image_unconfirmed_swaps_back_and_is_not_retried);
    RUN_TEST(test_a_confirmed_store_image_settles_and_is_not_copied_again);
    RUN_TEST(test_a_valid_confirm_request_settles_the_trial);
    RUN_TEST(test_a_stale_confirm_request_fails_the_trial);
    RUN_TEST(test_a_corrupt_confirm_request_fails_the_trial);
    RUN_TEST(test_a_trial_with_no_confirm_request_fails);
    RUN_TEST(test_a_torn_confirm_request_confirms_nothing);
    RUN_TEST(test_a_failed_trial_that_falls_back_forgets_its_mark);
    RUN_TEST(test_a_confirmed_store_trial_keeps_its_mark);
    RUN_TEST(test_a_store_image_below_the_floor_never_reaches_a_slot);
    RUN_TEST(test_a_fall_back_torn_before_the_swap_back_runs_it_at_the_next_boot);
    RUN_TEST(test_a_fall_back_over_a_block_already_failed_moves_nothing);
    RUN_TEST(test_a_commit_while_swapped_is_refused_untouched);
    RUN_TEST(test_a_journal_write_that_does_not_land_moves_nothing);
    return UNITY_END();
}
