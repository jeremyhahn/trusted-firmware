/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/boot_prev.c under a hostile store (#52, #75): the 328P's and 32U4's
 * fallback, driven by a seeded mutation loop over what anyone holding the
 * board can rewrite -- PREV in the external SPI NOR -- and over the block's
 * state and the store's bus, against the software NOR part on the mock boot
 * HAL's store bus, under the host board's (the 328P's) profile. Run plainly
 * and under the sanitizers (make test-asan), so an access outside the model's
 * or the firmware's buffers stops the run.
 *
 * Each reset is main()'s sequence around the swap seam (boot/boot_main.c):
 * the trial at reset and the swap seam's answer to it, then the evaluation,
 * a refused image's fall-back and its evaluation again, then recovery or the
 * jump. The evaluation is the test's, as test_boot_swap's is: boot_image.c's
 * trailer, binding and floor checks, the soft measurement and the verify
 * mock, which accepts one literal signature. An independent oracle -- the
 * trailer read by offset, SHA-256 over the measured span, the signature
 * compared -- must agree with it on every image the copy leaves.
 *
 * Deterministic: xorshift32 from a literal seed, a literal iteration count.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_stage.h"
#include "boot_swap.h"
#include "boot_trial.h"
#include "boot_verify.h"
#include "boot_verify_mock.h"
#include "sha256.h"
#include "../../model/spi_nor_model.h"

#define PREV       ((uint32_t)TFW_OTA_STORE_PREV_ADDR)
#define SEED       0x5EED52A1u
#define ITERATIONS 3000u
#define FLOOR      5u
#define PREV_VER   7u
#define NEW_VER    9u
#define IMG_LEN    384u
#define OP_READ    0x03u

#define TR (TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE)

typedef enum { OUT_BOOT, OUT_RECOVERY, OUT_RESET } outcome_t;

/* How the round reaches the fall-back. */
enum { S_TRIAL, S_REFUSED, S_STATE, S_COUNT };

/* What is done to PREV. */
enum {
    M_FLIPS, M_TRAILER_FLIPS, M_TRUNCATE, M_ZEROS, M_ONES, M_BOARD, M_BELOW_FLOOR,
    M_NONE, M_COUNT
};

/* What the store's bus or the region's flash does partway through. */
enum { F_NONE, F_CUT, F_WRITE_FAIL, F_DEAD, F_FLIP, F_COUNT };

static uint8_t             good_prev[TFW_BOOT_APP_SIZE];
static uint8_t             good_new[TFW_BOOT_APP_SIZE];
static uint8_t             bad_new[TFW_BOOT_APP_SIZE];
static uint8_t             hostile[TFW_BOOT_APP_SIZE];
static uint8_t             sig_ok[TFW_BOOT_SIG_LEN];
static tfw_boot_platform_t persist;
static unsigned            stores;
static uint32_t            rng;

static unsigned xfers;
static unsigned fault_at;
static unsigned fault_kind;
static unsigned copy_starts;
static char     where[96];

static unsigned ran;
static unsigned boots;
static unsigned recoveries;
static unsigned resets;
static unsigned restores;
static unsigned by_scenario[S_COUNT];
static unsigned by_fault[F_COUNT];

/* The block's write, the test's: boot_store.c stays out of the link. What it
 * holds is what the next reset finds. */
void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    persist = *p;
    stores++;
}

/* The flags byte is outside the modelled span: stored as the block is. */
void tfw_boot_platform_store_flags(const tfw_boot_platform_t *p, uint8_t flags)
{
    (void)flags;
    tfw_boot_platform_store(p);
}

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

/* The trailer at the region's end: the body's first `len` bytes measured,
 * signed with the one signature the verify mock accepts. */
static void seal(uint8_t *img, uint8_t board, uint32_t version, uint32_t len)
{
    uint8_t *t = &img[TR];

    memset(t, 0, TFW_BOOT_TRAILER_LEN);
    wr32(&t[TFW_BOOT_TR_MAGIC], TFW_BOOT_TRAILER_MAGIC);
    t[TFW_BOOT_TR_VERSION]     = (uint8_t)TFW_BOOT_TRAILER_VERSION;
    t[TFW_BOOT_TR_VERSION + 1] = (uint8_t)(TFW_BOOT_TRAILER_VERSION >> 8);
    t[TFW_BOOT_TR_BOARD_ID]    = board;
    wr32(&t[TFW_BOOT_TR_IMAGE_VERSION], version);
    wr32(&t[TFW_BOOT_TR_IMAGE_LEN], len);
    tfw_sha256(img, (uint16_t)len, &t[TFW_BOOT_TR_MEASUREMENT]);
    memcpy(&t[TFW_BOOT_TR_SIGNATURE], sig_ok, TFW_BOOT_SIG_LEN);
}

static void make_image(uint8_t *img, uint8_t salt, uint32_t version)
{
    uint32_t i;

    for (i = 0u; i < TR; i++) {
        img[i] = (uint8_t)(i * 5u + salt);
    }
    seal(img, (uint8_t)TFW_BOARD_ID, version, IMG_LEN);
}

/* The oracle: would a correct bootloader run this region? Written from the
 * trailer's layout, not from boot_image.c. */
static int bootable(const uint8_t *img, uint32_t floor)
{
    const uint8_t *t = &img[TR];
    uint32_t       version = rd32(&t[TFW_BOOT_TR_IMAGE_VERSION]);
    uint32_t       len     = rd32(&t[TFW_BOOT_TR_IMAGE_LEN]);
    uint8_t        m[TFW_BOOT_MEASURE_LEN];

    if (rd32(&t[TFW_BOOT_TR_MAGIC]) != TFW_BOOT_TRAILER_MAGIC
        || t[TFW_BOOT_TR_VERSION] != (uint8_t)TFW_BOOT_TRAILER_VERSION
        || t[TFW_BOOT_TR_VERSION + 1] != 0u
        || version == 0u || version < floor
        || len == 0u || len > TFW_BOOT_MAX_IMAGE_LEN
        || t[TFW_BOOT_TR_BOARD_ID] == 0u
        || t[TFW_BOOT_TR_BOARD_ID] != (uint8_t)TFW_BOARD_ID
        || (t[TFW_BOOT_TR_IMAGE_FLAGS] & (uint8_t)~TFW_BOOT_IMAGE_FLAGS_KNOWN) != 0u
        || memcmp(&t[TFW_BOOT_TR_SIGNATURE], sig_ok, TFW_BOOT_SIG_LEN) != 0) {
        return 0;
    }
    for (uint32_t i = TFW_BOOT_TR_RESERVED; i < TFW_BOOT_TRAILER_LEN; i++) {
        if (t[i] != 0u) {
            return 0;
        }
    }
    tfw_sha256(img, (uint16_t)len, m);
    return memcmp(m, &t[TFW_BOOT_TR_MEASUREMENT], sizeof m) == 0;
}

static tfw_status_t region_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    tfw_boot_flash_read(addr, buf, len);
    return TFW_OK;
}

/* main()'s evaluation of the region, the test's: boot_policy.c stays out of
 * the link, as in test_boot_prev. Checked against the oracle every time. */
static tfw_status_t gate(void)
{
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    uint8_t            m[TFW_BOOT_MEASURE_LEN];
    uint8_t            key[TFW_BOOT_PUBKEY_LEN];
    tfw_boot_trailer_t tr;
    tfw_status_t       st;
    uint8_t            half;

    for (half = 0u; half < TFW_BOOT_TRAILER_LEN; half = (uint8_t)(half + TFW_BOOT_TRAILER_LEN / 2u)) {
        tfw_boot_flash_read(TFW_BOOT_TRAILER_ADDR + half, &raw[half], TFW_BOOT_TRAILER_LEN / 2u);
    }
    memset(key, 0x5A, sizeof key);
    st = tfw_boot_trailer_parse(raw, &tr);
    if (st == TFW_OK) {
        st = tfw_boot_binding_permit(&tr, (uint8_t)TFW_BOARD_ID);
    }
    if (st == TFW_OK) {
        st = tfw_boot_rollback_permit(tr.image_version, FLOOR);
    }
    if (st == TFW_OK) {
        st = tfw_boot_measure_from(region_read, TFW_BOOT_APP_BASE, tr.image_len, m);
    }
    if (st == TFW_OK && memcmp(m, tr.measurement, sizeof m) != 0) {
        st = TFW_ERR_CRYPTO;
    }
    if (st == TFW_OK) {
        st = tfw_boot_verify(raw, TFW_BOOT_TR_SIGNED_LEN, tr.signature, key);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(bootable(&tfw_boot_mock_flash()[TFW_BOOT_APP_BASE], FLOOR),
                                  st == TFW_OK, where);
    return st;
}

/* The store's bus: the NOR part, a count of the copies of PREV begun, and the
 * fault the iteration drew -- a flipped bit in what a read returned, or a
 * store that stops answering. */
static void bus(uint8_t *buf, uint16_t len)
{
    xfers++;
    if (len >= 4u && buf[0] == OP_READ
        && (((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3]) == PREV) {
        copy_starts++;
    }
    tfw_nor_model_xfer(buf, len);
    if (xfers == fault_at) {
        if (fault_kind == F_FLIP && len > 4u) {
            buf[4u + next() % (uint32_t)(len - 4u)] ^= (uint8_t)(1u << (next() % 8u));
        } else if (fault_kind == F_DEAD) {
            tfw_boot_mock_on_store(NULL);
        }
    }
}

static void flash_region(const uint8_t *img)
{
    uint16_t ps = tfw_boot_flash_page_size();
    uint32_t a;

    for (a = 0u; a < TFW_BOOT_APP_SIZE; a += ps) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_flash_write_page(TFW_BOOT_APP_BASE + a, &img[a]));
    }
}

/* main()'s end of a trial that cannot go on: TRIAL, FAILED, stored. */
static outcome_t end_trial(tfw_boot_platform_t *plat)
{
    if (tfw_boot_trial_fail(plat) == TFW_OK) {
        tfw_boot_platform_store(plat);
    }
    return OUT_RECOVERY;
}

static void assert_copy_failed_kept_the_round(void)
{
    TEST_ASSERT_TRUE_MESSAGE(persist.boot_state == TFW_BOOT_STATE_TRIAL
                             || persist.ota_state == TFW_BOOT_OTA_PENDING, where);
}

/* One reset, as main() runs it around the swap seam. A power cut that lands
 * in a copy is a reset: the round stops there, with nothing after it run. */
static outcome_t reset_once(int cut_is_reset)
{
    tfw_boot_platform_t     plat  = persist;
    tfw_boot_reset_action_t trial = tfw_boot_trial_at_reset(&plat);
    tfw_boot_reset_action_t act;
    tfw_status_t            st;
    tfw_status_t            fb;
    unsigned                s0 = stores;

    copy_starts = 0u;
    act = tfw_boot_swap_at_reset(&plat, trial);
    if (trial == TFW_BOOT_RESET_FAIL && act == TFW_BOOT_RESET_FAIL) {
        /* The block is stored after the copy, never before. */
        TEST_ASSERT_EQUAL_UINT_MESSAGE(s0, stores, where);
        assert_copy_failed_kept_the_round();
        if (cut_is_reset) {
            return OUT_RESET;
        }
    }
    if (trial == TFW_BOOT_RESET_FAIL && act == TFW_BOOT_RESET_BOOT) {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(s0 + 1u, stores, where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_BOOT_STATE_GOOD, persist.boot_state, where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_BOOT_OTA_FAILED, persist.ota_state, where);
        restores++;
    }
    tfw_boot_stage_after_reset(trial, act);
    if (act == TFW_BOOT_RESET_FAIL) {
        return end_trial(&plat);
    }
    st = gate();
    if (act == TFW_BOOT_RESET_SETTLE) {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, copy_starts, where);
        return (st == TFW_OK) ? OUT_BOOT : OUT_RECOVERY;
    }
    if (st != TFW_OK && plat.ota_state == TFW_BOOT_OTA_PENDING) {
        (void)tfw_boot_stage_forget();
    }
    if (st != TFW_OK) {
        unsigned s1 = stores;

        fb = tfw_boot_swap_fall_back(&plat);
        if (fb == TFW_OK) {
            TEST_ASSERT_EQUAL_UINT_MESSAGE(s1 + 1u, stores, where);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_BOOT_STATE_GOOD, persist.boot_state, where);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_BOOT_OTA_FAILED, persist.ota_state, where);
            restores++;
            st = gate();
        } else {
            TEST_ASSERT_EQUAL_UINT_MESSAGE(s1, stores, where);
            if (fb != TFW_ERR_BAD_STATE) {
                assert_copy_failed_kept_the_round();
                if (cut_is_reset) {
                    return OUT_RESET;
                }
            }
        }
    }
    if (st != TFW_OK) {
        return end_trial(&plat);
    }
    if (plat.ota_state == TFW_BOOT_OTA_PENDING && tfw_boot_trial_arm(&plat) == TFW_OK) {
        tfw_boot_platform_store(&plat);
    }
    return OUT_BOOT;
}

static void mutate_prev(unsigned kind)
{
    uint32_t i;
    uint32_t n;

    memcpy(hostile, good_prev, sizeof hostile);
    switch (kind) {
    case M_FLIPS:
        n = 1u + next() % 8u;
        for (i = 0u; i < n; i++) {
            hostile[next() % sizeof hostile] ^= (uint8_t)(1u << (next() % 8u));
        }
        break;
    case M_TRAILER_FLIPS:
        n = 1u + next() % 4u;
        for (i = 0u; i < n; i++) {
            hostile[TR + next() % TFW_BOOT_TRAILER_LEN] ^= (uint8_t)(1u << (next() % 8u));
        }
        break;
    case M_TRUNCATE:
        n = next() % (uint32_t)sizeof hostile;
        memset(&hostile[n], 0xFF, sizeof hostile - n);
        break;
    case M_ZEROS:
        memset(hostile, 0x00, sizeof hostile);
        break;
    case M_ONES:
        memset(hostile, 0xFF, sizeof hostile);
        break;
    case M_BOARD:
        seal(hostile, (uint8_t)(TFW_BOARD_ID ^ (1u + next() % 255u)), PREV_VER, IMG_LEN);
        break;
    case M_BELOW_FLOOR:
        seal(hostile, (uint8_t)TFW_BOARD_ID, next() % FLOOR, IMG_LEN);
        break;
    default:
        break;
    }
}

static void begin(void)
{
    tfw_boot_mock_reset();
    tfw_nor_model_reset();
    tfw_boot_mock_on_store(bus);
    tfw_boot_verify_mock_want(sig_ok);
    xfers       = 0u;
    fault_at    = 0u;
    fault_kind  = F_NONE;
    copy_starts = 0u;
    stores      = 0u;
    memset(&persist, 0, sizeof persist);
    persist.rollback_floor = FLOOR;
    persist.boot_state     = TFW_BOOT_STATE_GOOD;
    persist.ota_state      = TFW_BOOT_OTA_PENDING;
}

static void check_round(outcome_t out)
{
    /* At most one copy of PREV a round, and the floor never moves. */
    TEST_ASSERT_TRUE_MESSAGE(copy_starts <= 1u, where);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(FLOOR, persist.rollback_floor, where);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, tfw_nor_model_violations(), where);
    if (out == OUT_BOOT) {
        /* What runs is what a correct bootloader would run: the verify mock's
         * one signature, this board, at or above the floor, measured. */
        TEST_ASSERT_TRUE_MESSAGE(bootable(&tfw_boot_mock_flash()[TFW_BOOT_APP_BASE], FLOOR), where);
        boots++;
    } else if (out == OUT_RECOVERY) {
        recoveries++;
    } else {
        resets++;
    }
}

static void one_iteration(unsigned it)
{
    unsigned            scenario = next() % S_COUNT;
    unsigned            kind     = next() % M_COUNT;
    unsigned            fault    = next() % F_COUNT;
    tfw_boot_platform_t at_start;
    outcome_t           out;
    unsigned            round;
    int                 was_reset = 0;

    begin();
    mutate_prev(kind);
    tfw_nor_model_load(PREV, hostile, sizeof hostile);
    if (scenario == S_TRIAL) {
        flash_region(good_new);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&persist));
    } else if (scenario == S_REFUSED) {
        flash_region(bad_new);
    } else {
        flash_region((next() & 1u) ? good_new : bad_new);
        persist.boot_state = (uint8_t)next();
        persist.ota_state  = (uint8_t)next();
        if ((next() & 1u) != 0u) {
            persist.boot_state = TFW_BOOT_STATE_TRIAL;
        }
    }
    at_start = persist;
    fault_kind = fault;
    if (fault == F_CUT) {
        tfw_boot_mock_cut_after(next() % (TFW_BOOT_APP_SIZE / tfw_boot_flash_page_size() + 4u));
    } else if (fault == F_WRITE_FAIL) {
        tfw_boot_mock_fail_writes(1u + next() % 3u);
    } else if (fault == F_DEAD || fault == F_FLIP) {
        fault_at = 1u + next() % 300u;
    }
    snprintf(where, sizeof where, "it %u scenario %u mutation %u fault %u state %02X/%02X",
             it, scenario, kind, fault, at_start.boot_state, at_start.ota_state);

    /* The hostile reset, then a second over whatever it left, then a third:
     * the store answers again and the power stays on. */
    for (round = 0u; round < 3u; round++) {
        out = reset_once(fault == F_CUT && round == 0u);
        if (round == 1u && was_reset) {
            /* Cut short, redone from the start at the next reset. */
            TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, copy_starts, where);
        }
        was_reset = (out == OUT_RESET);
        if (copy_starts > 0u) {
            TEST_ASSERT_TRUE_MESSAGE(at_start.boot_state == TFW_BOOT_STATE_TRIAL
                                     || at_start.ota_state == TFW_BOOT_OTA_PENDING, where);
        }
        check_round(out);
        tfw_boot_mock_cut_after(UINT_MAX);
        tfw_boot_mock_fail_writes(0u);
        tfw_boot_mock_on_store(bus);
        fault_at = 0u;
    }
    by_scenario[scenario]++;
    by_fault[fault]++;
    ran++;
}

void setUp(void)
{
    unsigned i;

    for (i = 0u; i < sizeof sig_ok; i++) {
        sig_ok[i] = (uint8_t)(0xC3u ^ (i * 7u));
    }
    make_image(good_prev, 1u, PREV_VER);
    make_image(good_new, 9u, NEW_VER);
    memcpy(bad_new, good_new, sizeof bad_new);
    bad_new[TR + TFW_BOOT_TR_SIGNATURE] ^= 0x01u;
    begin();
}

void tearDown(void) {}

static void test_a_hostile_prev_never_boots_what_the_gate_refuses(void)
{
    unsigned i;

    rng = SEED;
    for (i = 0u; i < ITERATIONS; i++) {
        one_iteration(i);
    }
    /* The loop ran, and reached every path it is about. */
    TEST_ASSERT_EQUAL_UINT(ITERATIONS, ran);
    TEST_ASSERT_NOT_EQUAL(0u, boots);
    TEST_ASSERT_NOT_EQUAL(0u, recoveries);
    TEST_ASSERT_NOT_EQUAL(0u, resets);
    TEST_ASSERT_NOT_EQUAL(0u, restores);
    for (i = 0u; i < S_COUNT; i++) {
        TEST_ASSERT_NOT_EQUAL(0u, by_scenario[i]);
    }
    for (i = 0u; i < F_COUNT; i++) {
        TEST_ASSERT_NOT_EQUAL(0u, by_fault[i]);
    }
}

/* After the hostile run, a clean PREV restores and boots as test_boot_prev
 * pins it: the region is PREV, one store, GOOD with FAILED, no violation. */
static void test_after_the_hostile_run_a_clean_prev_restores_and_boots(void)
{
    rng = SEED;
    one_iteration(0u);
    begin();
    tfw_nor_model_load(PREV, good_prev, sizeof good_prev);
    flash_region(good_new);
    stores = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&persist));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT,
                      tfw_boot_swap_at_reset(&persist, tfw_boot_trial_at_reset(&persist)));
    TEST_ASSERT_EQUAL_MEMORY(good_prev, &tfw_boot_mock_flash()[TFW_BOOT_APP_BASE], sizeof good_prev);
    TEST_ASSERT_EQUAL_UINT(1u, stores);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, persist.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, persist.ota_state);
    TEST_ASSERT_EQUAL_UINT32(FLOOR, persist.rollback_floor);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
    snprintf(where, sizeof where, "clean prev");
    TEST_ASSERT_EQUAL(TFW_OK, gate());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_hostile_prev_never_boots_what_the_gate_refuses);
    RUN_TEST(test_after_the_hostile_run_a_clean_prev_restores_and_boots);
    return UNITY_END();
}
