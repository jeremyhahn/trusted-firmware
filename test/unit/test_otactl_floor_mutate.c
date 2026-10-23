/*
 * OTA_INFO's rollback floor (#163) under seeded mutation, linked as
 * test_otactl_floor is: each tier's reader by its renamed name, against the
 * software element.
 *
 * The element tier's floor is its boot counter, so a floor request is a
 * Counter Increment and the store is the part: mutated requests (another
 * mode, another key id) and mutated answers (a flipped response byte, a
 * dropped command, a part that stays busy, refuses or leaves the bus). The
 * test keeps the range the counter can be in from what the part accepted;
 * every floor read must land in it, never below the last one read, and a
 * read that fails leaves the caller's word alone.
 *
 * The soft tier's floor is the platform block's copy as its caller hands it
 * over; checking that block is its loader's job, outside this link.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "atecc608a_model.h"
#include "atecc.h"
#include "tfw/boot.h"
#include "tfw/protocol.h"
#include "../../src/core/ota_floor.h"

tfw_status_t tfw_otactl_floor_element(const tfw_boot_platform_t *p, uint32_t *out);
tfw_status_t tfw_otactl_floor_soft(const tfw_boot_platform_t *p, uint32_t *out);

#define SEED        0x0F100163u
#define RUNS        8u
#define STEPS       500u
#define BLOCKS      2000u
#define SENTINEL    0xA5A5A5A5u
#define COUNTER_MAX 0x1FFFFFu       /* the part's counters stop at 2^21 - 1 */

static tfw_boot_platform_t plat;
static uint32_t rng;
static uint32_t lo, hi, last;       /* the counter is in [lo, hi]; last read */
static uint32_t reads_ok, reads_refused, writes_ok, writes_refused;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void scramble_platform(void)
{
    uint8_t *b = (uint8_t *)&plat;
    size_t   i;

    for (i = 0u; i < sizeof plat; i++) {
        b[i] = (uint8_t)next();
    }
}

static void clean(void)
{
    tfw_model_force_status(0u);
    tfw_model_set_absent(0u);
    tfw_model_set_busy(0u);
    tfw_model_corrupt_next_read(0u, 0u);
}

/* One mutation of the part's answer, or none, for the next operation. */
static void mutate(void)
{
    switch (next() % 8u) {
    case 0:
    case 1:
        tfw_model_corrupt_next_read((uint8_t)(next() % 8u), (uint8_t)(1u + next() % 255u));
        break;
    case 2:
        tfw_model_nack_next_command();
        break;
    case 3:
        tfw_model_set_busy((uint8_t)(next() % 80u));
        break;
    case 4:
        tfw_model_force_status((uint8_t)(1u + next() % 255u));
        break;
    case 5:
        tfw_model_set_absent(1u);
        break;
    default:
        break;
    }
}

static void read_floor(void)
{
    uint32_t     out = SENTINEL;
    tfw_status_t st;

    scramble_platform();                    /* the element tier ignores the block */
    st = tfw_otactl_floor_element(&plat, &out);
    if (st != TFW_OK) {
        TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, st);
        TEST_ASSERT_EQUAL_HEX32(SENTINEL, out);
        reads_refused++;
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(out >= lo && out <= hi, "a floor no write produced");
    TEST_ASSERT_TRUE_MESSAGE(out >= last, "the floor moved down");
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    lo = hi = last = out;
    reads_ok++;
}

/* A floor request: Counter, mostly Increment on the boot counter, sometimes
 * another mode byte or key id. */
static void request_floor(void)
{
    uint8_t      p1 = (next() % 4u) ? TFW_ATECC_COUNTER_INCR : (uint8_t)next();
    uint16_t     p2 = (next() % 4u) ? (uint16_t)TFW_ATECC_COUNTER_BOOT : (uint16_t)next();
    uint8_t      v[4];
    uint32_t     got;
    tfw_status_t st;

    memset(v, 0x5A, sizeof v);
    st = tfw_atecc_wake();
    if (st == TFW_OK) {
        st = tfw_atecc_exec(TFW_ATECC_OP_COUNTER, p1, p2, 0, 0u, v, 4u, TFW_ATECC_T_COUNTER);
        (void)tfw_atecc_sleep();
    }
    if (st != TFW_OK) {
        if (p1 == TFW_ATECC_COUNTER_INCR) {
            hi++;                           /* it may have counted before the answer broke */
        }
        writes_refused++;
        return;
    }
    got = (uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    if (p1 == TFW_ATECC_COUNTER_INCR) {
        TEST_ASSERT_TRUE(got >= lo + 1u && got <= hi + 1u);
    } else {
        TEST_ASSERT_TRUE(got >= lo && got <= hi);
    }
    lo = hi = got;
    writes_ok++;
}

void setUp(void)
{
    tfw_model_reset();
    memset(&plat, 0, sizeof plat);
    rng = SEED;
    reads_ok = reads_refused = writes_ok = writes_refused = 0u;
}
void tearDown(void) {}

static void test_mutated_requests_and_answers_never_move_the_floor_down(void)
{
    uint32_t run, n;

    for (run = 0u; run < RUNS; run++) {
        uint32_t stored = next() % (COUNTER_MAX / 2u);

        if (run == 0u) {
            stored = 0u;
        }
        tfw_model_reset();
        tfw_model_set_counter(stored);      /* a stored floor from anywhere */
        lo = hi = last = stored;
        for (n = 0u; n < STEPS; n++) {
            mutate();
            if (next() % 2u) {
                read_floor();
            } else {
                request_floor();
            }
            clean();
        }
        /* A clean sequence afterwards reads as pinned: the floor the part
         * holds, then one more for one increment, and the part asleep. */
        {
            uint32_t before = reads_ok;
            uint32_t was;

            read_floor();
            TEST_ASSERT_EQUAL_UINT32(before + 1u, reads_ok);
            was = last;
            TEST_ASSERT_TRUE(was <= stored + STEPS);
            request_floor();
            read_floor();
            TEST_ASSERT_EQUAL_UINT32(before + 2u, reads_ok);
            TEST_ASSERT_TRUE(last == was || last == was + 1u);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(RUNS * (STEPS + 3u), reads_ok + reads_refused + writes_ok + writes_refused);
    TEST_ASSERT_TRUE(reads_ok > RUNS * STEPS / 8u);
    TEST_ASSERT_TRUE(reads_refused > RUNS * STEPS / 8u);
    TEST_ASSERT_TRUE(writes_ok > RUNS * STEPS / 8u);
    TEST_ASSERT_TRUE(writes_refused > RUNS * STEPS / 8u);
}

/* The soft tier reports the block's floor word as handed over, whatever the
 * rest of the block holds, and never wakes the element. */
static void test_a_mutated_platform_block_reports_its_own_floor_word(void)
{
    uint32_t n;

    for (n = 0u; n < BLOCKS; n++) {
        uint32_t out = SENTINEL;

        scramble_platform();
        if ((n % 8u) == 0u) {
            plat.rollback_floor = (n & 8u) ? 0xFFFFFFFFu : 0u;
        }
        TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor_soft(&plat, &out));
        TEST_ASSERT_EQUAL_HEX32(plat.rollback_floor, out);
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_requests_and_answers_never_move_the_floor_down);
    RUN_TEST(test_a_mutated_platform_block_reports_its_own_floor_word);
    return UNITY_END();
}
