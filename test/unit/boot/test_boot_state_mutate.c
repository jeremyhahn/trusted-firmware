/*
 * tfw-boot's EEPROM state under hostile contents (#77).
 *
 * The platform block and the update request sit in memory anyone with the
 * board in hand can rewrite, CRC and all. Each iteration writes a valid block
 * the way test_boot_settle does, then rewrites it -- bit flips, all 0x00, all
 * 0xFF, random bytes, a torn copy of another block, or hostile states and
 * counters under a correct CRC -- and runs what tfw-boot does at reset: the
 * load, the floor, the trial's decision and the steps after it, with the
 * floor's write torn at random.
 *
 * The contract, whatever the block holds:
 *   - every call returns, and reads and writes nothing outside the buffers it
 *     owns (ASan in `make test-asan` is the check);
 *   - a block that fails its magic, layout or CRC is not taken;
 *   - the decision is one of the four actions;
 *   - the floor never moves down: not in RAM, not in the block written back,
 *     not in the element's counter;
 *   - an image whose trial failed is never made pending again;
 *   - a request is honoured only as encoded after a watchdog reset, and is
 *     cleared whatever it held.
 * After the hostile runs a clean block settles and boots as test_boot_settle
 * pins it.
 *
 * Deterministic: xorshift32 from a literal seed, a fixed iteration count.
 * Linked as test_boot_settle is: the real boot_store.c over the mock EEPROM,
 * the element's counter the test's.
 */
#include <string.h>

#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_cdi.h"
#include "boot_platform.h"
#include "boot_request.h"
#include "boot_store.h"
#include "boot_trial.h"

#define ITERS 4000u

static int                 el_present;
static uint32_t            el_counter;
static tfw_boot_platform_t plat;
static uint32_t            rng;

/* The element's monotonic counter, when the row has one. */
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
    if (el_counter < value) {
        el_counter = value;   /* counts up only, as the part does */
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

static uint32_t edge32(void)
{
    static const uint32_t e[] = { 0u, 1u, 2u, 0x7FFFFFFFuL, 0x80000000uL, 0xFFFFFFFEuL, 0xFFFFFFFFuL };
    uint32_t r = next();

    return ((r & 1u) != 0u) ? e[(r >> 1) % (sizeof e / sizeof e[0])] : next() % 64u;
}

static uint8_t any_state(void)
{
    uint32_t r = next() % 8u;

    return (r < 6u) ? (uint8_t)r : (uint8_t)next();   /* each known value, then any byte */
}

/* A valid block, its fields chosen from the edges, serialised as
 * test_boot_platform builds one. */
static void valid(uint8_t raw[TFW_BOOT_EE_LEN], uint8_t hostile)
{
    tfw_boot_platform_t p;

    memset(raw, 0xFF, TFW_BOOT_EE_LEN);
    memset(&p, 0, sizeof p);
    memset(p.pubkey_hash, 0x11, sizeof p.pubkey_hash);
    memset(p.serial, 0x22, sizeof p.serial);
    memset(p.operator_pubkey, 0x33, sizeof p.operator_pubkey);
    p.rollback_floor = 4u;
    p.boot_count     = 17u;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    p.fail_count     = 2u;
    if (hostile != 0u) {
        p.rollback_floor = edge32();
        p.boot_count     = edge32();
        p.boot_state     = any_state();
        p.ota_state      = any_state();
        p.fail_count     = (uint16_t)edge32();
    }
    tfw_boot_platform_serialise(&p, raw);
}

static void hostile_block(uint8_t raw[TFW_BOOT_EE_LEN])
{
    uint8_t  other[TFW_BOOT_EE_LEN];
    uint32_t i;
    uint32_t n;

    switch (next() % 7u) {
    case 0:   /* states and counters at the edges, under a correct CRC */
        valid(raw, 1u);
        break;
    case 1:
        valid(raw, next() & 1u);
        for (n = 1u + next() % 4u; n > 0u; n--) {
            raw[next() % TFW_BOOT_EE_LEN] ^= (uint8_t)(1u << (next() % 8u));
        }
        break;
    case 2:
        memset(raw, 0x00, TFW_BOOT_EE_LEN);
        break;
    case 3:
        memset(raw, 0xFF, TFW_BOOT_EE_LEN);
        break;
    case 4:
        for (i = 0u; i < TFW_BOOT_EE_LEN; i++) {
            raw[i] = (uint8_t)next();
        }
        break;
    case 5:   /* torn: a write of another block stopped part way */
        valid(raw, 0u);
        valid(other, 1u);
        memcpy(raw, other, next() % TFW_BOOT_EE_LEN);
        break;
    default:
        valid(raw, 0u);
        break;
    }
}

static tfw_status_t reload(void)
{
    tfw_status_t st;

    memset(&plat, 0, sizeof plat);
    tfw_boot_platform_load(&plat, &st);
    return st;
}

static void assert_action(tfw_boot_reset_action_t a)
{
    TEST_ASSERT_TRUE(a == TFW_BOOT_RESET_FAIL || a == TFW_BOOT_RESET_SETTLE ||
                     a == TFW_BOOT_RESET_TRY || a == TFW_BOOT_RESET_BOOT);
}

/* One step the boot or the application's commands take on the block. */
static void step(uint32_t *floor, const uint8_t *meas)
{
    uint8_t  other[TFW_BOOT_MEASURE_LEN];
    uint32_t before = *floor;

    memset(other, 0x44, sizeof other);
    switch (next() % 7u) {
    case 0: (void)tfw_boot_trial_arm(&plat); break;
    case 1: (void)tfw_boot_trial_fail(&plat); break;
    case 2: (void)tfw_boot_trial_fall_back(&plat); break;
    case 3: (void)tfw_boot_trial_confirm(&plat); break;
    case 4: (void)tfw_boot_trial_activate(&plat, (next() & 1u) ? meas : other, meas); break;
    case 5: (void)tfw_boot_trial_begin_permit(&plat); break;
    default:
        if ((next() % 4u) == 0u) {
            tfw_boot_mock_tear_nvm(next() % 3u, (uint8_t)(next() % TFW_BOOT_EE_LEN), (uint8_t)next());
        }
        (void)tfw_boot_settle(&plat, edge32(), floor);
        break;
    }
    TEST_ASSERT_TRUE(*floor >= before);
    TEST_ASSERT_TRUE(plat.rollback_floor >= before || el_present);
}

void setUp(void)
{
    tfw_boot_mock_reset();
    el_present = 0;
    el_counter = 0u;
}

void tearDown(void) {}

static void test_a_hostile_block_never_lowers_the_floor_or_retries_a_failed_image(void)
{
    uint8_t  raw[TFW_BOOT_EE_LEN];
    uint8_t  meas[TFW_BOOT_MEASURE_LEN];
    uint32_t i;
    unsigned taken   = 0u;
    unsigned refused = 0u;

    memset(meas, 0x5Au, sizeof meas);
    rng = 0x9E3779B9uL;
    for (i = 0u; i < ITERS; i++) {
        tfw_status_t st;
        uint32_t     floor0;
        uint32_t     floor;
        uint32_t     n;

        tfw_boot_mock_reset();
        el_present = (int)(next() & 1u);
        el_counter = edge32();
        hostile_block(raw);
        memcpy(tfw_boot_mock_nvm(), raw, TFW_BOOT_EE_LEN);

        st = reload();
        if (st != TFW_OK) {
            TEST_ASSERT_TRUE(st == TFW_ERR_NOT_PROVISIONED || st == TFW_ERR_UNSUPPORTED ||
                             st == TFW_ERR_CRC);
            refused++;
            continue;
        }
        taken++;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor0));
        floor = floor0;

        assert_action(tfw_boot_trial_at_reset(&plat));
        for (n = 1u + next() % 6u; n > 0u; n--) {
            uint32_t el_before = el_counter;

            step(&floor, meas);
            assert_action(tfw_boot_trial_at_reset(&plat));
            TEST_ASSERT_TRUE(el_counter >= el_before);
        }

        /* The image whose trial failed is never pending again. */
        if (tfw_boot_trial_fail(&plat) == TFW_OK) {
            TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&plat));
            TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_trial_activate(&plat, meas, meas));
            TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_trial_arm(&plat));
        }

        /* What the next reset reads back holds the floor too. */
        if (reload() == TFW_OK) {
            uint32_t now;

            TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &now));
            TEST_ASSERT_TRUE(now >= floor0);
        }
    }
    TEST_ASSERT_EQUAL_UINT(ITERS, taken + refused);
    TEST_ASSERT_TRUE(taken > 0u);
    TEST_ASSERT_TRUE(refused > 0u);
}

static void test_a_hostile_request_is_honoured_only_as_encoded(void)
{
    uint8_t  enc[TFW_BOOT_REQUEST_LEN];
    uint8_t  req[TFW_BOOT_REQUEST_LEN];
    uint32_t i;
    uint32_t k;
    unsigned honoured = 0u;

    tfw_boot_request_encode(enc);
    rng = 0x6A09E667uL;
    for (i = 0u; i < ITERS; i++) {
        tfw_reset_cause_t cause = (tfw_reset_cause_t)(next() % 7u);
        uint8_t           valid;

        tfw_boot_mock_reset();
        memcpy(req, enc, sizeof req);
        switch (next() % 5u) {
        case 0:  req[next() % sizeof req] ^= (uint8_t)(1u << (next() % 8u)); break;
        case 1:  memset(req, 0x00, sizeof req); break;
        case 2:  memset(req, 0xFF, sizeof req); break;
        case 3:
            for (k = 0u; k < sizeof req; k++) {
                req[k] = (uint8_t)next();
            }
            break;
        default: break;
        }
        tfw_boot_mock_set_handoff(req, sizeof req);

        valid = tfw_boot_request_take(cause);
        TEST_ASSERT_EQUAL_UINT8((cause == TFW_RESET_WATCHDOG && memcmp(req, enc, sizeof req) == 0) ? 1u : 0u,
                                valid);
        for (k = 0u; k < TFW_BOOT_REQUEST_LEN; k++) {
            TEST_ASSERT_EQUAL_HEX8(0u, tfw_boot_mock_handoff()[k]);
        }
        honoured += valid;
    }
    TEST_ASSERT_TRUE(honoured > 0u);
    TEST_ASSERT_TRUE(honoured < ITERS);
}

/* After the hostile runs, a clean block settles to GOOD and boots, as
 * test_boot_settle's soft row pins it. */
static void test_a_clean_block_after_the_hostile_runs_settles_and_boots(void)
{
    uint8_t  raw[TFW_BOOT_EE_LEN];
    uint32_t floor;

    tfw_boot_mock_reset();
    memset(raw, 0xFF, sizeof raw);
    memset(&plat, 0, sizeof plat);
    plat.boot_state     = TFW_BOOT_STATE_CONFIRMED;
    plat.ota_state      = TFW_BOOT_OTA_IDLE;
    plat.rollback_floor = 3u;
    tfw_boot_platform_serialise(&plat, raw);
    memcpy(tfw_boot_mock_nvm(), raw, TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, reload());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u, floor);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT32(7u, floor);
    TEST_ASSERT_EQUAL(TFW_OK, reload());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT32(7u, plat.rollback_floor);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&plat));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_hostile_block_never_lowers_the_floor_or_retries_a_failed_image);
    RUN_TEST(test_a_hostile_request_is_honoured_only_as_encoded);
    RUN_TEST(test_a_clean_block_after_the_hostile_runs_settles_and_boots);
    return UNITY_END();
}
