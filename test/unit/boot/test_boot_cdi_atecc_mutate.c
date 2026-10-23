/*
 * Mutation tests for boot/tier/cdi_atecc.c (#163). test_boot_cdi_atecc drives
 * the tier through the real driver and the software element, which answers
 * like the part but cannot fail one chosen call and does not keep time. This
 * link has neither: the test file is the driver's wake, exec and sleep and the
 * I2C init, so a test fails exactly the call it names and sees every frame,
 * execution time, bus rate and sleep the tier asks for.
 *
 * No device, no key: the HMAC answer is a fixed pattern, not a derivation.
 */
#include <string.h>
#include "unity.h"
#include "boot_cdi.h"
#include "boot_image.h"
#include "tfw/hal.h"
#include "tfw/types.h"
#include "atecc.h"

#define MAX_LOG 64u

typedef struct {
    uint8_t  op, p1;
    uint16_t p2;
    uint8_t  data[TFW_BOOT_MEASURE_LEN];
    uint8_t  dlen, rlen;
    uint16_t ms;
    uint8_t  awake;                     /* was the part awake when it came */
} cmd_t;

static unsigned     inits, wakes, sleeps, wake_before_init;
static uint32_t     init_hz;
static uint8_t      awake;
static uint32_t     wake_fail;          /* bit n: the n-th wake (from 0) fails */
static uint8_t      exec_fail_at;       /* the command index that fails, 0xFF none */
static tfw_status_t exec_fail_st;
static cmd_t        log_[MAX_LOG];
static uint8_t      log_n;
static uint32_t     counter0, counter1;

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    inits++;
    init_hz = hz;
    return TFW_OK;
}

tfw_status_t tfw_atecc_wake(void)
{
    unsigned n = wakes++;

    if (inits == 0u) {
        wake_before_init++;
    }
    if (n < 32u && (wake_fail & (1uL << n))) {
        return TFW_ERR_NACK;
    }
    awake = 1u;
    return TFW_OK;
}

tfw_status_t tfw_atecc_sleep(void)
{
    sleeps++;
    awake = 0u;
    return TFW_OK;
}

static void put_le(uint8_t *r, uint8_t rlen, uint32_t v)
{
    uint8_t i;

    for (i = 0u; i < rlen && i < 4u; i++) {
        r[i] = (uint8_t)(v >> (8u * i));
    }
}

tfw_status_t tfw_atecc_exec(uint8_t opcode, uint8_t p1, uint16_t p2,
                          const uint8_t *data, uint8_t dlen,
                          uint8_t *resp, uint8_t rlen, uint16_t exec_ms)
{
    uint8_t  idx = log_n;
    cmd_t   *c   = &log_[log_n < MAX_LOG ? log_n : MAX_LOG - 1u];
    uint8_t  i;

    if (log_n < MAX_LOG) {
        log_n++;
    }
    memset(c, 0, sizeof *c);
    c->op = opcode; c->p1 = p1; c->p2 = p2; c->dlen = dlen; c->rlen = rlen;
    c->ms = exec_ms; c->awake = awake;
    if (data && dlen <= sizeof c->data) {
        memcpy(c->data, data, dlen);
    }
    if (idx == exec_fail_at) {
        return exec_fail_st;
    }
    if (opcode == TFW_ATECC_OP_COUNTER) {
        uint32_t *ctr = (p2 == 0u) ? &counter0 : &counter1;
        if (p1 == TFW_ATECC_COUNTER_INCR) {
            (*ctr)++;
        }
        put_le(resp, rlen, *ctr);
    } else if (opcode == TFW_ATECC_OP_SHA && p1 == TFW_ATECC_SHA_HMAC_END) {
        for (i = 0u; i < rlen; i++) {
            resp[i] = (uint8_t)((i < dlen ? data[i] : 0u) ^ 0xA5u);
        }
    }
    return TFW_OK;
}

void setUp(void)
{
    inits = wakes = sleeps = wake_before_init = 0u;
    init_hz = 0u;
    awake = 0u;
    wake_fail = 0u;
    exec_fail_at = 0xFFu;
    exec_fail_st = TFW_OK;
    memset(log_, 0, sizeof log_);
    log_n = 0u;
    counter0 = 0u;
    counter1 = 0x00AA0000uL;
}
void tearDown(void) {}

/* Init with a part that answers, then forget init's own traffic. */
static void present(uint32_t counter)
{
    counter0 = counter;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    wakes = sleeps = 0u;
}

static void assert_asleep_and_paired(void)
{
    uint8_t i;

    TEST_ASSERT_FALSE(awake);
    TEST_ASSERT_EQUAL_UINT(wakes, sleeps);
    for (i = 0u; i < log_n; i++) {
        TEST_ASSERT_TRUE_MESSAGE(log_[i].awake, "a command to a sleeping part");
    }
}

static void assert_counter_cmd(uint8_t i, uint8_t p1)
{
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_COUNTER, log_[i].op);
    TEST_ASSERT_EQUAL_HEX8(p1, log_[i].p1);
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_COUNTER_BOOT, log_[i].p2);
    TEST_ASSERT_EQUAL_UINT8(0u, log_[i].dlen);
    TEST_ASSERT_EQUAL_UINT8(4u, log_[i].rlen);
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_T_COUNTER, log_[i].ms);
}

/* ---- init and tier ---- */

static void test_init_sets_the_wake_rate_before_the_first_wake(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL_UINT(1u, inits);
    TEST_ASSERT_EQUAL_UINT32(TFW_ATECC_I2C_HZ, init_hz);
    TEST_ASSERT_TRUE(init_hz <= 100000uL);
    TEST_ASSERT_EQUAL_UINT(0u, wake_before_init);
}

static void test_a_part_that_wakes_is_put_back_to_sleep(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL_UINT(1u, wakes);
    TEST_ASSERT_EQUAL_UINT(1u, sleeps);
    TEST_ASSERT_FALSE(awake);
    TEST_ASSERT_EQUAL_UINT8(0u, log_n);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SECURE_ELEMENT, tfw_boot_cdi_tier());
}

static void test_a_part_that_does_not_wake_is_no_tier_and_boots(void)
{
    wake_fail = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL_UINT(0u, sleeps);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_NONE, tfw_boot_cdi_tier());
}

static void test_a_later_init_reads_the_part_again(void)
{
    present(0u);
    wake_fail = 1u;                         /* the next wake, init's */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_NONE, tfw_boot_cdi_tier());
}

/* ---- derive ---- */

static void test_derive_sends_start_on_the_uds_then_end_with_the_measurement(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN], want[TFW_BOOT_MEASURE_LEN];
    uint8_t i;

    for (i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        m[i]    = (uint8_t)(0x40u + i);
        want[i] = (uint8_t)(m[i] ^ 0xA5u);
    }
    memset(cdi, 0, sizeof cdi);
    present(0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_UINT8(2u, log_n);

    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_SHA, log_[0].op);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_SHA_HMAC_START, log_[0].p1);
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_SLOT_UDS, log_[0].p2);
    TEST_ASSERT_EQUAL_UINT8(0u, log_[0].dlen);
    TEST_ASSERT_EQUAL_UINT8(0u, log_[0].rlen);
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_T_SHA, log_[0].ms);

    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_SHA, log_[1].op);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_SHA_HMAC_END, log_[1].p1);
    TEST_ASSERT_EQUAL_UINT16(TFW_BOOT_MEASURE_LEN, log_[1].p2);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_MEASURE_LEN, log_[1].dlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(m, log_[1].data, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_MEASURE_LEN, log_[1].rlen);
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_T_SHA, log_[1].ms);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, cdi, sizeof cdi);
    TEST_ASSERT_EQUAL_UINT(1u, wakes);
    assert_asleep_and_paired();
}

static void test_derive_without_a_part_touches_nothing(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN] = {0}, cdi[TFW_BOOT_MEASURE_LEN];

    wake_fail = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    wakes = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_UINT(0u, wakes);
    TEST_ASSERT_EQUAL_UINT8(0u, log_n);
}

static void test_derive_that_cannot_wake_sends_nothing(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN] = {0}, cdi[TFW_BOOT_MEASURE_LEN];

    present(0u);
    wake_fail = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_UINT8(0u, log_n);
}

static void test_a_refused_start_sends_no_end_and_still_sleeps(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN] = {0}, cdi[TFW_BOOT_MEASURE_LEN];

    present(0u);
    exec_fail_at = 0u;
    exec_fail_st = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_UINT8(1u, log_n);
    assert_asleep_and_paired();
}

static void test_a_refused_end_is_its_status_and_still_sleeps(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN] = {0}, cdi[TFW_BOOT_MEASURE_LEN];

    present(0u);
    exec_fail_at = 1u;
    exec_fail_st = TFW_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_UINT8(2u, log_n);
    assert_asleep_and_paired();
}

/* ---- floor read ---- */

static void test_the_floor_read_is_one_counter_read_little_endian(void)
{
    uint32_t v = 0u;

    present(0x0A0B0C0Du);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL_HEX32(0x0A0B0C0Du, v);
    TEST_ASSERT_EQUAL_UINT8(1u, log_n);
    assert_counter_cmd(0u, TFW_ATECC_COUNTER_READ);
    TEST_ASSERT_EQUAL_UINT(1u, wakes);
    assert_asleep_and_paired();
}

static void test_no_part_has_no_floor_and_sends_nothing(void)
{
    uint32_t v = 9u;

    wake_fail = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    wakes = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_floor_write(10u));
    TEST_ASSERT_EQUAL_UINT32(9u, v);
    TEST_ASSERT_EQUAL_UINT(0u, wakes);
    TEST_ASSERT_EQUAL_UINT8(0u, log_n);
}

static void test_a_floor_read_that_cannot_wake_sends_nothing(void)
{
    uint32_t v = 9u;

    present(4u);
    wake_fail = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL_UINT32(9u, v);
    TEST_ASSERT_EQUAL_UINT8(0u, log_n);
}

static void test_a_refused_floor_read_is_crypto_and_still_sleeps(void)
{
    uint32_t v = 9u;

    present(4u);
    exec_fail_at = 0u;
    exec_fail_st = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL_UINT32(9u, v);
    assert_asleep_and_paired();
}

/* ---- floor write ---- */

static void test_the_floor_write_reads_then_increments_until_it_holds(void)
{
    uint8_t i;

    present(5u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(8u));
    TEST_ASSERT_EQUAL_UINT32(8u, counter0);
    TEST_ASSERT_EQUAL_UINT32(0x00AA0000uL, counter1);
    TEST_ASSERT_EQUAL_UINT8(7u, log_n);     /* read, incr x3, final read */
    for (i = 0u; i < 6u; i += 2u) {
        assert_counter_cmd(i, TFW_ATECC_COUNTER_READ);
        assert_counter_cmd((uint8_t)(i + 1u), TFW_ATECC_COUNTER_INCR);
    }
    assert_counter_cmd(6u, TFW_ATECC_COUNTER_READ);
    TEST_ASSERT_EQUAL_UINT(7u, wakes);
    assert_asleep_and_paired();
}

static void test_a_floor_already_there_is_one_read(void)
{
    present(5u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(5u));
    TEST_ASSERT_EQUAL_UINT8(1u, log_n);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(4u));
    TEST_ASSERT_EQUAL_UINT8(2u, log_n);
    TEST_ASSERT_EQUAL_UINT32(5u, counter0);
}

static void test_one_below_takes_one_increment(void)
{
    present(5u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(6u));
    TEST_ASSERT_EQUAL_UINT32(6u, counter0);
    TEST_ASSERT_EQUAL_UINT8(3u, log_n);
}

static void test_a_refused_read_in_the_write_increments_nothing(void)
{
    present(5u);
    exec_fail_at = 0u;
    exec_fail_st = TFW_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_write(6u));
    TEST_ASSERT_EQUAL_UINT8(1u, log_n);
    TEST_ASSERT_EQUAL_UINT32(5u, counter0);
    assert_asleep_and_paired();
}

static void test_a_lost_wake_before_the_increment_sends_no_increment(void)
{
    present(5u);
    wake_fail = 1u << 1;                    /* the read wakes, the increment does not */
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_write(6u));
    TEST_ASSERT_EQUAL_UINT8(1u, log_n);
    TEST_ASSERT_EQUAL_UINT32(5u, counter0);
}

static void test_a_refused_increment_stops_the_write(void)
{
    present(5u);
    exec_fail_at = 1u;
    exec_fail_st = TFW_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_write(6u));
    TEST_ASSERT_EQUAL_UINT8(2u, log_n);
    assert_asleep_and_paired();
}

static void test_the_step_bound_is_exact(void)
{
    present(0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(TFW_BOOT_FLOOR_MAX_STEP));
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_FLOOR_MAX_STEP, counter0);

    setUp();
    present(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_floor_write(TFW_BOOT_FLOOR_MAX_STEP + 1u));
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_FLOOR_MAX_STEP + 1u, counter0);
    TEST_ASSERT_EQUAL_UINT8(2u * (TFW_BOOT_FLOOR_MAX_STEP + 1u), log_n);
    assert_asleep_and_paired();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_the_wake_rate_before_the_first_wake);
    RUN_TEST(test_a_part_that_wakes_is_put_back_to_sleep);
    RUN_TEST(test_a_part_that_does_not_wake_is_no_tier_and_boots);
    RUN_TEST(test_a_later_init_reads_the_part_again);
    RUN_TEST(test_derive_sends_start_on_the_uds_then_end_with_the_measurement);
    RUN_TEST(test_derive_without_a_part_touches_nothing);
    RUN_TEST(test_derive_that_cannot_wake_sends_nothing);
    RUN_TEST(test_a_refused_start_sends_no_end_and_still_sleeps);
    RUN_TEST(test_a_refused_end_is_its_status_and_still_sleeps);
    RUN_TEST(test_the_floor_read_is_one_counter_read_little_endian);
    RUN_TEST(test_no_part_has_no_floor_and_sends_nothing);
    RUN_TEST(test_a_floor_read_that_cannot_wake_sends_nothing);
    RUN_TEST(test_a_refused_floor_read_is_crypto_and_still_sleeps);
    RUN_TEST(test_the_floor_write_reads_then_increments_until_it_holds);
    RUN_TEST(test_a_floor_already_there_is_one_read);
    RUN_TEST(test_one_below_takes_one_increment);
    RUN_TEST(test_a_refused_read_in_the_write_increments_nothing);
    RUN_TEST(test_a_lost_wake_before_the_increment_sends_no_increment);
    RUN_TEST(test_a_refused_increment_stops_the_write);
    RUN_TEST(test_the_step_bound_is_exact);
    return UNITY_END();
}
