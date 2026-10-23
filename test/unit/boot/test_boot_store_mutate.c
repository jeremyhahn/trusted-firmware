/*
 * boot/boot_store.c under the faults its callers cannot see (#163): the floor
 * one step up, an EEPROM write the cell refuses, an element that stops part
 * way, then the same settle over seeded floors, versions and faults. Each case
 * kills a mutant test_boot_settle and test_boot_state_mutate let live: a
 * settle that stores only a step of two or more, and a block write whose
 * refusal is reported as TFW_OK.
 */
#include <limits.h>
#include <string.h>

#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_cdi.h"
#include "boot_image.h"
#include "boot_platform.h"
#include "boot_store.h"
#include "boot_trial.h"

#define ITERS 3000u

static int                 el_present;
static uint32_t            el_counter;
static int                 el_fail;
static unsigned            el_incr_left;
static tfw_boot_platform_t plat;
static uint32_t            rng;

/* The element's counter: one increment at a time, failing once `incr_left`
 * increments have been spent, as test_boot_settle's does. */
tfw_status_t tfw_boot_floor_read(uint32_t *out)
{
    if (!el_present) {
        return TFW_ERR_UNSUPPORTED;
    }
    *out = el_counter;
    return TFW_OK;
}

tfw_status_t tfw_boot_floor_write(uint32_t value)
{
    if (!el_present) {
        return TFW_ERR_UNSUPPORTED;
    }
    while (el_counter < value) {
        if (el_fail && el_incr_left == 0u) {
            el_fail = 0;
            return TFW_ERR_CRYPTO;
        }
        if (el_incr_left > 0u) {
            el_incr_left--;
        }
        el_counter++;
    }
    return TFW_OK;
}

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    tfw_boot_mock_reset();
    memset(&plat, 0, sizeof plat);
    el_present   = 0;
    el_counter   = 0u;
    el_fail      = 0;
    el_incr_left = 0u;
}

void tearDown(void) {}

static tfw_status_t reload(void)
{
    tfw_status_t st;

    memset(&plat, 0, sizeof plat);
    tfw_boot_platform_load(&plat, &st);
    return st;
}

static void reset_with(uint8_t boot_state, uint32_t ee_floor)
{
    uint8_t ee[TFW_BOOT_EE_LEN];

    memset(ee, 0xFF, sizeof ee);
    memset(&plat, 0, sizeof plat);
    plat.boot_state     = boot_state;
    plat.ota_state      = TFW_BOOT_OTA_IDLE;
    plat.rollback_floor = ee_floor;
    tfw_boot_platform_serialise(&plat, ee);
    memcpy(tfw_boot_mock_nvm(), ee, TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, reload());
}

/* The next EEPROM write lands nothing and fails: torn at its first byte, with
 * that byte's old value left in it. */
static void refuse_next_nvm_write(void)
{
    tfw_boot_mock_tear_nvm(0u, 0u, tfw_boot_mock_nvm()[0]);
}

/* ------------------------------------------------------- one step of floor */

/* Mutant: `next != *floor` -> `next > *floor + 1u`. */
void test_the_soft_floor_moves_by_one(void)
{
    uint32_t floor;

    reset_with(TFW_BOOT_STATE_CONFIRMED, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 4u, &floor));
    TEST_ASSERT_EQUAL_UINT32(4u, floor);
    TEST_ASSERT_EQUAL(TFW_OK, reload());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT32(4u, plat.rollback_floor);
}

void test_the_element_counter_moves_by_one(void)
{
    uint32_t floor;

    el_present = 1;
    el_counter = 3u;
    reset_with(TFW_BOOT_STATE_CONFIRMED, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 4u, &floor));
    TEST_ASSERT_EQUAL_UINT32(4u, floor);
    TEST_ASSERT_EQUAL_UINT32(4u, el_counter);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(4u, floor);
}

/* The widest step the floor takes, and one past it, which settles GOOD at the
 * old floor: the version is refused as a floor, not the image. */
void test_the_widest_step_moves_the_floor_and_one_past_it_does_not(void)
{
    uint32_t floor;

    reset_with(TFW_BOOT_STATE_CONFIRMED, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 3u + TFW_BOOT_FLOOR_MAX_STEP, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u + TFW_BOOT_FLOOR_MAX_STEP, floor);
    TEST_ASSERT_EQUAL(TFW_OK, reload());
    TEST_ASSERT_EQUAL_UINT32(3u + TFW_BOOT_FLOOR_MAX_STEP, plat.rollback_floor);

    reset_with(TFW_BOOT_STATE_CONFIRMED, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 4u + TFW_BOOT_FLOOR_MAX_STEP, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u, floor);
    TEST_ASSERT_EQUAL(TFW_OK, reload());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT32(3u, plat.rollback_floor);
}

/* --------------------------------------------------- a refused block write */

/* Mutant: platform_write returns TFW_OK whatever tfw_boot_nvm_write said. */
void test_a_refused_eeprom_write_is_not_a_stored_floor(void)
{
    uint8_t  before[TFW_BOOT_EE_LEN];
    uint32_t floor;

    reset_with(TFW_BOOT_STATE_GOOD, 3u);
    memcpy(before, tfw_boot_mock_nvm(), sizeof before);
    refuse_next_nvm_write();

    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_floor_store(&plat, 7u));
    TEST_ASSERT_EQUAL_UINT32(3u, plat.rollback_floor);
    TEST_ASSERT_EQUAL_MEMORY(before, tfw_boot_mock_nvm(), sizeof before);
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, reload());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u, floor);
}

void test_a_settle_whose_floor_write_is_refused_stays_confirmed(void)
{
    uint32_t floor;

    reset_with(TFW_BOOT_STATE_CONFIRMED, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    refuse_next_nvm_write();

    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u, floor);
    TEST_ASSERT_EQUAL_UINT32(3u, plat.rollback_floor);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_CONFIRMED, plat.boot_state);

    /* the next reset, the cell taking writes again, settles */
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, reload());
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, tfw_boot_trial_at_reset(&plat));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL(TFW_OK, reload());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT32(7u, plat.rollback_floor);
}

/* --------------------------------------------- seeded floors and faults */

static uint32_t pick_floor(void)
{
    static const uint32_t e[] = { 0u, 1u, 2u, 0x7FFFFFFFuL, 0xFFFFFFF0uL };
    uint32_t r = next();

    return ((r & 3u) == 0u) ? e[(r >> 2) % (sizeof e / sizeof e[0])] : next() % 1000u;
}

static uint32_t pick_version(uint32_t f)
{
    static const uint32_t d[] = { 0u, 1u, 2u, TFW_BOOT_FLOOR_MAX_STEP - 1u,
                                  TFW_BOOT_FLOOR_MAX_STEP, TFW_BOOT_FLOOR_MAX_STEP + 1u, 1000u };
    uint32_t r = next();
    uint32_t delta;

    if ((r & 7u) == 0u) {
        return (f == 0u) ? 0u : f - 1u - (next() % f);    /* below the floor */
    }
    delta = d[(r >> 3) % (sizeof d / sizeof d[0])];
    return (0xFFFFFFFFuL - f < delta) ? f : f + delta;
}

void test_seeded_settles_store_the_floor_the_authority_holds(void)
{
    unsigned stepped = 0u, refused = 0u, held = 0u;

    rng = 0x243F6A88uL;
    for (uint32_t i = 0u; i < ITERS; i++) {
        uint8_t      before[TFW_BOOT_EE_LEN];
        uint32_t     f_ee, f, floor, v, want;
        unsigned     fault;
        int          bites;
        tfw_status_t st;

        setUp();
        el_present = (int)(next() & 1u);
        el_counter = pick_floor();
        f_ee       = pick_floor();
        reset_with(TFW_BOOT_STATE_CONFIRMED, f_ee);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &f));
        TEST_ASSERT_EQUAL_UINT32(el_present ? el_counter : f_ee, f);
        floor = f;
        v     = pick_version(f);
        want  = (v > f && v - f <= TFW_BOOT_FLOOR_MAX_STEP) ? v : f;

        fault = next() % 3u;
        if (fault == 1u) {
            refuse_next_nvm_write();
        } else if (fault == 2u && el_present) {
            el_fail      = 1;
            el_incr_left = next() % (TFW_BOOT_FLOOR_MAX_STEP + 2u);
        }
        /* The fault reaches the floor's authority only on a step. */
        bites = (want != f) &&
                ((fault == 1u && !el_present) ||
                 (fault == 2u && el_present && el_incr_left < want - f));
        memcpy(before, tfw_boot_mock_nvm(), sizeof before);

        st = tfw_boot_settle(&plat, v, &floor);
        tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);

        if (bites) {
            refused++;
            TEST_ASSERT_NOT_EQUAL(TFW_OK, st);
            TEST_ASSERT_EQUAL_UINT32(f, floor);
            TEST_ASSERT_EQUAL_UINT32(f_ee, plat.rollback_floor);
            TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_CONFIRMED, plat.boot_state);
            TEST_ASSERT_EQUAL_MEMORY(before, tfw_boot_mock_nvm(), sizeof before);
            if (el_present) {
                TEST_ASSERT_TRUE(el_counter >= f && el_counter < want);
            }
            continue;
        }
        TEST_ASSERT_EQUAL(TFW_OK, st);
        TEST_ASSERT_EQUAL_UINT32(want, floor);
        if (want != f) {
            stepped++;
        } else {
            held++;
        }
        TEST_ASSERT_EQUAL(TFW_OK, reload());
        /* A refused GOOD write on a no-step settle leaves CONFIRMED to retry. */
        if (fault == 1u) {
            TEST_ASSERT_TRUE(plat.boot_state == TFW_BOOT_STATE_GOOD ||
                             plat.boot_state == TFW_BOOT_STATE_CONFIRMED);
        } else {
            TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
        }
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
        TEST_ASSERT_EQUAL_UINT32(want, floor);
    }
    TEST_ASSERT_TRUE(stepped > 0u);
    TEST_ASSERT_TRUE(refused > 0u);
    TEST_ASSERT_TRUE(held > 0u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_soft_floor_moves_by_one);
    RUN_TEST(test_the_element_counter_moves_by_one);
    RUN_TEST(test_the_widest_step_moves_the_floor_and_one_past_it_does_not);
    RUN_TEST(test_a_refused_eeprom_write_is_not_a_stored_floor);
    RUN_TEST(test_a_settle_whose_floor_write_is_refused_stays_confirmed);
    RUN_TEST(test_seeded_settles_store_the_floor_the_authority_holds);
    return UNITY_END();
}
